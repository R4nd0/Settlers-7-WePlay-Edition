"""Offline trust-boundary checks; never contacts Discord or reads a real token."""
import asyncio
import copy
import importlib.util
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from unittest.mock import AsyncMock,Mock,patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/discord'))
import bot_core as core
spec=importlib.util.spec_from_file_location('weplay_discord_bot',ROOT/'tools/discord/bot.py')
bot=importlib.util.module_from_spec(spec);spec.loader.exec_module(bot)


class CoreTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.path=Path(self.temp.name)/'feed.json'
        self.value={'format':core.FEED_FORMAT,'observed_at':1000,'rooms':[
            {'public_id':'a'*32,'title':'Gemeinschaftsraum','players':2,'capacity':8,'sessions':1}]}

    def write(self,value=None):
        self.path.write_text(json.dumps(self.value if value is None else value),encoding='utf-8')

    def test_all_three_public_ids_are_bound(self):
        config=core.settings(ROOT/'tools/discord/settings.example.json')
        self.assertEqual((config.application_id,config.guild_id,config.channel_id),
                         (1565350833854353459,1565306702356815962,1565306705535959042))

    def test_settings_reject_secrets_extra_keys_and_noncanonical_ids(self):
        value=json.loads((ROOT/'tools/discord/settings.example.json').read_text(encoding='utf-8'))
        for change in ({'token':'synthetic-secret'},{'application_id':123},{'guild_id':'0'+'1'*18},
                       {'transport':'http'},{'token_environment':'PATH'}):
            self.write(dict(value,**change))
            with self.subTest(change=change),self.assertRaises(core.BotError):core.settings(self.path)

    def test_duplicate_json_keys_are_rejected(self):
        self.path.write_text('{"rooms":[],"rooms":[]}')
        with self.assertRaises(core.BotError):core.record(self.path,1024)

    def test_public_projection_omits_internal_room_identifier(self):
        self.write();result=core.public_status(self.path,now=1001)
        self.assertEqual(result,[{'title':'Gemeinschaftsraum','players':2,'capacity':8,'sessions':1}])
        self.assertNotIn('a'*32,json.dumps(result))

    def test_missing_stale_future_or_invalid_feed_never_claims_an_empty_list(self):
        self.assertIn('Datenquelle',core.listing(self.path)['description'])
        for stamp in (900,1006,float('nan'),True):
            self.write(dict(self.value,observed_at=stamp))
            with patch.object(core.time,'time',return_value=1000):
                self.assertIn('Datenquelle',core.listing(self.path)['description'])

    def test_false_counts_secrets_mentions_urls_and_unknown_state_are_rejected(self):
        for change in ({'players':True},{'players':9},{'capacity':9},{'sessions':-1},
                       {'title':'weplay1.synthetic-secret'},{'title':'@everyone'},
                       {'title':'https://example.invalid'},{'network_secret':'no'}, {'state':'running'}):
            value=copy.deepcopy(self.value);value['rooms'][0].update(change);self.write(value)
            with self.subTest(change=change),self.assertRaises(core.BotError):core.public_status(self.path,now=1000)

    def test_empty_and_unavailable_are_distinct_and_match_state_is_not_inferred(self):
        self.write()
        with patch.object(core.time,'time',return_value=1000):
            message=core.listing(self.path)
            self.assertIn('nicht verifiziert',message['fields'][0]['value'])
            self.write(dict(self.value,rooms=[]))
            self.assertIn('keine öffentlichen Räume',core.listing(self.path)['description'])

    def test_maximum_rendered_projection_fits_discord_embed_limits(self):
        value=copy.deepcopy(self.value)
        value['rooms']=[dict(value['rooms'][0],public_id=f'{i:032x}',title='*'*80) for i in range(16)]
        self.write(value)
        with patch.object(core.time,'time',return_value=1000):message=core.listing(self.path)
        chars=len(message['title'])+len(message['description'])+sum(len(x['name'])+len(x['value']) for x in message['fields'])
        self.assertLessEqual(chars,6000);self.assertTrue(all(len(x['name'])<=256 for x in message['fields']))

    def test_oversized_file_and_duplicate_room_are_rejected(self):
        self.path.write_bytes(b'x'*65537)
        with self.assertRaises(core.BotError):core.public_status(self.path,now=1000)
        self.value['rooms']*=2;self.write()
        with self.assertRaises(core.BotError):core.public_status(self.path,now=1000)

    def test_instance_lock_refuses_parallel_bot_and_releases_after_exit(self):
        directory=Path(self.temp.name)/'state'
        with bot.instance_lock(directory):
            with self.assertRaises(core.BotError):
                with bot.instance_lock(directory):pass
        with bot.instance_lock(directory):pass

    def test_offline_check_neither_prompts_nor_opens_a_client(self):
        with patch.object(bot,'getpass') as prompt,patch.object(bot,'client') as client,redirect_stdout(io.StringIO()):
            self.assertEqual(bot.main(['--check']),0)
        prompt.assert_not_called();client.assert_not_called()

    def test_library_errors_never_echo_token_and_environment_copy_is_removed(self):
        secret='synthetic-secret-'*4
        with patch.dict(os.environ,{core.TOKEN_ENVIRONMENT:secret}), \
                patch.object(bot,'client') as factory,redirect_stdout(io.StringIO()) as output:
            factory.return_value.run.side_effect=RuntimeError(secret)
            result=bot.main(['--state',str(Path(self.temp.name)/'state')])
            self.assertNotIn(core.TOKEN_ENVIRONMENT,os.environ)
        self.assertEqual(result,2);self.assertNotIn(secret,output.getvalue())


class GatewayTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        import discord
        self.discord=discord
        self.config=core.settings(ROOT/'tools/discord/settings.example.json')
        self.client=bot.client(self.config,None)

    async def asyncTearDown(self):
        await self.client.close()

    async def test_only_guild_commands_and_no_privileged_intents(self):
        self.assertEqual(self.client.tree.get_commands(),[])
        self.assertEqual({x.name for x in self.client.tree.get_commands(guild=self.discord.Object(self.config.guild_id))},
                         {'spiele','weplay-status','weplay-hilfe'})
        self.assertEqual(self.client.intents.value,1)
        self.assertTrue(all(getattr(self.client.allowed_mentions,key) is False for key in
                            ('everyone','users','roles','replied_user')))

    async def test_wrong_application_token_fails_before_command_registration(self):
        info=type('Info',(),{'id':1})()
        with patch.object(self.client,'application_info',new=AsyncMock(return_value=info)), \
                patch.object(self.client.tree,'sync',new=AsyncMock()) as sync:
            with self.assertRaisesRegex(core.BotError,'application_mismatch'):await self.client.setup_hook()
        sync.assert_not_called()

    async def test_startup_validates_channel_guild_before_syncing_only_target_guild(self):
        info=type('Info',(),{'id':self.config.application_id})()
        channel=Mock(spec=self.discord.TextChannel);channel.guild.id=1
        with patch.object(self.client,'application_info',new=AsyncMock(return_value=info)), \
                patch.object(self.client,'fetch_channel',new=AsyncMock(return_value=channel)) as fetch, \
                patch.object(self.client.tree,'sync',new=AsyncMock()) as sync:
            with self.assertRaisesRegex(core.BotError,'channel_invalid'):await self.client.setup_hook()
            sync.assert_not_called()
            channel.guild.id=self.config.guild_id
            await self.client.setup_hook()
            self.assertEqual(fetch.call_args.args,(self.config.channel_id,))
            self.assertEqual(sync.call_args.kwargs['guild'].id,self.config.guild_id)

    async def test_command_guard_rejects_wrong_channel_and_direct_messages(self):
        for guild,channel in ((None,None),(self.config.guild_id,1),(1,self.config.channel_id)):
            interaction=type('Interaction',(),{'guild_id':guild,'channel_id':channel,'response':AsyncMock()})()
            self.assertFalse(await self.client.tree.interaction_check(interaction))
            self.assertTrue(interaction.response.send_message.call_args.kwargs['ephemeral'])
        interaction=type('Interaction',(),{'guild_id':self.config.guild_id,'channel_id':self.config.channel_id})()
        self.assertTrue(await self.client.tree.interaction_check(interaction))


if __name__=='__main__':unittest.main()
