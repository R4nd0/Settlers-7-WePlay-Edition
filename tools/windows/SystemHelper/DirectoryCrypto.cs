using System.Security.Cryptography;
using System.Text.Json;

// Transient join keys and plaintext only travel over inherited private stdio,
// never command arguments, diagnostic messages, persistent files or the server.
internal static class DirectoryCrypto
{
    private const string Format="weplay-join-crypto-v1",Scheme="rsa3072-oaep256-a256gcm-v1";
    private static void Exact(JsonElement value,params string[] keys)
    {
        if(value.ValueKind!=JsonValueKind.Object)throw new InvalidDataException("helper_crypto_invalid");
        var names=value.EnumerateObject().Select(x=>x.Name).ToArray();
        if(names.Length!=keys.Length||names.Distinct().Count()!=names.Length||!keys.All(name=>value.TryGetProperty(name,out _)))
            throw new InvalidDataException("helper_crypto_invalid");
    }
    private static byte[] Read64(JsonElement value,string name,int maximum)
    {
        string text=value.GetProperty(name).GetString()??throw new InvalidDataException("helper_crypto_invalid");
        if(text.Length==0||text.Length>maximum*2||text.Any(c=>!(char.IsAsciiLetterOrDigit(c)||c is '-' or '_')))
            throw new InvalidDataException("helper_crypto_invalid");
        byte[] bytes=Convert.FromBase64String(text.Replace('-','+').Replace('_','/')+new string('=',(4-text.Length%4)%4));
        if(bytes.Length>maximum||Encode(bytes)!=text)throw new InvalidDataException("helper_crypto_invalid");
        return bytes;
    }
    private static string Encode(byte[] value)=>Convert.ToBase64String(value).TrimEnd('=').Replace('+','-').Replace('/','_');
    internal static object Run(string operation)
    {
        using var input=Console.OpenStandardInput();using var memory=new MemoryStream();
        var buffer=new byte[4096];int count;
        while((count=input.Read(buffer))>0){if(memory.Length+count>16384)throw new InvalidDataException("helper_crypto_invalid");memory.Write(buffer,0,count);}
        using var document=JsonDocument.Parse(memory.ToArray());var request=document.RootElement;
        if(request.GetProperty("format").GetString()!=Format)throw new InvalidDataException("helper_crypto_invalid");
        using RSA rsa=RSA.Create();
        if(operation=="keygen")
        {
            Exact(request,"format");rsa.KeySize=3072;
            byte[] secret=rsa.ExportPkcs8PrivateKey();
            try{return new {format=Format,public_key=Encode(rsa.ExportSubjectPublicKeyInfo()),private_key=Encode(secret)};}
            finally{CryptographicOperations.ZeroMemory(secret);}
        }
        if(operation=="seal")
        {
            Exact(request,"format","public_key","plaintext","associated");
            byte[] publicKey=Read64(request,"public_key",1024);rsa.ImportSubjectPublicKeyInfo(publicKey,out int used);
            if(used!=publicKey.Length||rsa.KeySize!=3072)throw new InvalidDataException("helper_crypto_invalid");
            byte[] plaintext=Read64(request,"plaintext",4096),associated=Read64(request,"associated",1024);
            byte[] key=RandomNumberGenerator.GetBytes(32),nonce=RandomNumberGenerator.GetBytes(12),tag=new byte[16],ciphertext=new byte[plaintext.Length];
            try
            {
                using(var aes=new AesGcm(key,16))aes.Encrypt(nonce,plaintext,ciphertext,tag,associated);
                return new {scheme=Scheme,key=Encode(rsa.Encrypt(key,RSAEncryptionPadding.OaepSHA256)),
                    nonce=Encode(nonce),ciphertext=Encode(ciphertext),tag=Encode(tag)};
            }
            finally{CryptographicOperations.ZeroMemory(key);CryptographicOperations.ZeroMemory(plaintext);}
        }
        if(operation=="open")
        {
            Exact(request,"format","private_key","envelope","associated");
            byte[] secret=Read64(request,"private_key",4096);
            try{rsa.ImportPkcs8PrivateKey(secret,out int used);if(used!=secret.Length||rsa.KeySize!=3072)throw new InvalidDataException("helper_crypto_invalid");}
            finally{CryptographicOperations.ZeroMemory(secret);}
            var envelope=request.GetProperty("envelope");Exact(envelope,"scheme","key","nonce","ciphertext","tag");
            if(envelope.GetProperty("scheme").GetString()!=Scheme)throw new InvalidDataException("helper_crypto_invalid");
            byte[] key=rsa.Decrypt(Read64(envelope,"key",384),RSAEncryptionPadding.OaepSHA256);
            byte[] nonce=Read64(envelope,"nonce",12),tag=Read64(envelope,"tag",16),ciphertext=Read64(envelope,"ciphertext",4096);
            byte[] associated=Read64(request,"associated",1024),plaintext=new byte[ciphertext.Length];
            if(key.Length!=32||nonce.Length!=12||tag.Length!=16)throw new InvalidDataException("helper_crypto_invalid");
            try
            {
                using(var aes=new AesGcm(key,16))aes.Decrypt(nonce,ciphertext,tag,plaintext,associated);
                return new {format=Format,plaintext=Encode(plaintext)};
            }
            finally{CryptographicOperations.ZeroMemory(key);CryptographicOperations.ZeroMemory(plaintext);}
        }
        throw new InvalidDataException("helper_crypto_invalid");
    }
}
