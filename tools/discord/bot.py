"""Guild-scoped Gateway bot. No HTTP listener, user tokens or game control."""
import argparse
import asyncio
from contextlib import contextmanager
from getpass import getpass
import logging
import os
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from bot_core import BotError, COMMUNITY_URL, TOKEN_ENVIRONMENT, listing, plain, settings


@contextmanager
def instance_lock(directory):
    directory = plain(directory); directory.mkdir(mode=0o700, parents=True, exist_ok=True)
    with plain(directory/'instance.lock').open('a+b') as stream:
        stream.seek(0,2)
        if not stream.tell():stream.write(b'0');stream.flush()
        stream.seek(0)
        try:
            if os.name == 'nt':
                import msvcrt
                msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(stream.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:raise BotError('already_running') from None
        try:yield
        finally:
            stream.seek(0)
            if os.name == 'nt':msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)
            else:fcntl.flock(stream.fileno(), fcntl.LOCK_UN)


def client(config, feed):
    import discord
    from discord import app_commands

    class Tree(app_commands.CommandTree):
        async def interaction_check(self, interaction):
            if interaction.guild_id == config.guild_id and interaction.channel_id == config.channel_id:
                return True
            await interaction.response.send_message(
                f'Dieser Bot ist nur in <#{config.channel_id}> verfügbar.',ephemeral=True)
            return False

        async def on_error(self, interaction, error):
            # Never print interaction options, exception details or credentials.
            print('WePlay-Bot: Befehlsfehler.',flush=True)
            try:
                if not interaction.response.is_done():
                    await interaction.response.send_message('Der Befehl konnte nicht ausgeführt werden.',ephemeral=True)
            except discord.DiscordException:pass

    class WePlay(discord.Client):
        def __init__(self):
            intents=discord.Intents.none();intents.guilds=True
            super().__init__(intents=intents,application_id=config.application_id,
                             allowed_mentions=discord.AllowedMentions.none(),max_messages=None)
            self.tree=Tree(self);self.checked=False;self.failure=False
            guild=discord.Object(config.guild_id)

            @self.tree.command(name='spiele',description='Zeigt die ausdrücklich veröffentlichten WePlay-Räume.',guild=guild)
            @app_commands.guild_only()
            async def games(interaction: discord.Interaction):
                await interaction.response.defer(thinking=True)
                embed=discord.Embed.from_dict(await asyncio.to_thread(listing,feed))
                await interaction.followup.send(embed=embed,allowed_mentions=discord.AllowedMentions.none())

            @self.tree.command(name='weplay-status',description='Prüft den WePlay-Bot in diesem Kanal.',guild=guild)
            @app_commands.guild_only()
            async def status(interaction: discord.Interaction):
                await interaction.response.send_message(
                    'WePlay-Bot verbunden. Spielzustand und Internetpartien werden dadurch nicht bestätigt.',ephemeral=True)

            @self.tree.command(name='weplay-hilfe',description='Hilfe zu privaten Einladungen und zur Raumübersicht.',guild=guild)
            @app_commands.guild_only()
            async def help_(interaction: discord.Interaction):
                await interaction.response.send_message(
                    'Im WePlay-Starter: Internet spielen → Raum erstellen → Für Discord kopieren. '
                    'Teile private Codes nur mit deinen Mitspielern. Die Raumübersicht benötigt eine '
                    'ausdrücklich veröffentlichte Datenquelle. Community: '+COMMUNITY_URL,ephemeral=True)

        async def setup_hook(self):
            # Refuse a token for another application before registering commands.
            info=await self.application_info()
            if info.id != config.application_id:raise BotError('application_mismatch')
            channel=await self.fetch_channel(config.channel_id)
            if not isinstance(channel,discord.TextChannel) or channel.guild.id != config.guild_id:
                raise BotError('channel_invalid')
            # Dedicated WePlay application's command set, scoped only to this guild.
            await self.tree.sync(guild=discord.Object(config.guild_id))

        async def on_ready(self):
            if self.checked:return
            guild=self.get_guild(config.guild_id)
            channel=self.get_channel(config.channel_id)
            if guild is None or not isinstance(channel,discord.TextChannel):
                self.failure=True
                print('WePlay-Bot: Zielkanal nicht verfügbar.',flush=True);await self.close();return
            member=guild.me or await guild.fetch_member(self.user.id)
            permissions=channel.permissions_for(member)
            if not all(getattr(permissions,key) for key in
                       ('view_channel','send_messages','embed_links','read_message_history')):
                self.failure=True
                print('WePlay-Bot: Kanalberechtigungen fehlen.',flush=True);await self.close();return
            self.checked=True
            print('WePlay-Bot verbunden. /spiele, /weplay-status und /weplay-hilfe sind bereit.',flush=True)

        async def on_error(self,event_method,*args,**kwargs):
            print('WePlay-Bot: Verbindungsereignis fehlgeschlagen.',flush=True)
    return WePlay()


def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--settings',type=Path,default=Path(__file__).with_name('settings.example.json'))
    parser.add_argument('--state',type=Path,default=Path(__file__).resolve().parents[2]/'work/discord-bot-state')
    parser.add_argument('--feed',type=Path,help='Optional: ausschließlich öffentliche, geprüfte Raumdaten.')
    parser.add_argument('--check',action='store_true',help='Nur lokale Konfiguration prüfen; keine Anmeldung.')
    args=parser.parse_args(argv)
    try:
        config=settings(args.settings)
        if args.check:
            print('Bot-Konfiguration gültig. Keine Discord-Verbindung aufgebaut.');return 0
        logging.disable(logging.CRITICAL)
        with instance_lock(args.state):
            # User enters this locally in a real console; never passed as an
            # argument or stored in a file. No prompt in unattended execution.
            token=os.environ.pop(TOKEN_ENVIRONMENT,'').strip()
            if not token:
                if not sys.stdin.isatty():raise BotError('token_missing')
                token=getpass('Discord-Bot-Token (lokal, Eingabe unsichtbar): ').strip()
            if not 20 <= len(token) <= 512 or any(c.isspace() for c in token):raise BotError('token_invalid')
            bot=client(config,plain(args.feed) if args.feed else None)
            try:bot.run(token,log_handler=None)
            finally:token=''
            if bot.failure:return 2
        return 0
    except KeyboardInterrupt:return 130
    except BotError as error:
        print('WePlay-Bot: '+str(error)+'.',flush=True);return 2
    except Exception:
        print('WePlay-Bot: Start fehlgeschlagen. Token, Installation und Kanalrechte lokal prüfen.',flush=True)
        return 2


if __name__=='__main__':raise SystemExit(main())
