"""Offer native public games without linking a Discord account.

The per-run hosting secret stays in memory. Only public native metadata and
encrypted join replies reach the same bounded directory used by readers.
"""
import http.client
import secrets

from discord_directory import Reporter
from game_directory import DirectoryError,configured,request
from join_crypto import encoded


class PublicReporter(Reporter):
    report_path='/v1/directory/report'
    warning_code='directory_unavailable'

    def __init__(self,*args,**kwargs):
        self.registration_connection=kwargs.get('connection_factory',http.client.HTTPSConnection)
        super().__init__(*args,**kwargs)
        self.last_registration=self.clock()

    def register(self,url,key):
        response=request(url,'/v1/directory/register','POST',
            dict(format='weplay-public-host-v1',token=key),connection_factory=self.registration_connection)
        if not isinstance(response,dict) or set(response)!={'ok'} or response['ok'] is not True:raise DirectoryError('directory_invalid')

    def credentials(self,state):
        url=configured(state)
        if url is None:raise DirectoryError('directory_not_connected')
        key=encoded(secrets.token_bytes(32))
        self.register(url,key)
        return url,key

    def poll(self):
        if self.clock()-self.last_registration>=60:
            self.register(self.url,self.key);self.last_registration=self.clock()
        super().poll()
