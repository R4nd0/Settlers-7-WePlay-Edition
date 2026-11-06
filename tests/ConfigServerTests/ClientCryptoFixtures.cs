using System.Diagnostics;
using System.Security.Cryptography;

// Independent test peer: Windows BCrypt or the Linux OpenSSL CLI. All material
// is synthetic. Production code never invokes this class or an external cipher.
internal static class ClientCryptoFixtures
{
    internal static bool Supported => OperatingSystem.IsWindows() || OperatingSystem.IsLinux();
    internal static string Provider => OperatingSystem.IsWindows() ? "Windows BCrypt" : "OpenSSL CLI";
    private static byte[] OpenSsl(byte[] input, params string[] arguments)
    {
        ProcessStartInfo start = new("/usr/bin/openssl")
        {
            RedirectStandardInput = true, RedirectStandardOutput = true,
            RedirectStandardError = true, UseShellExecute = false
        };
        foreach (string argument in arguments) start.ArgumentList.Add(argument);
        using Process process = Process.Start(start) ?? throw new Exception("OpenSSL test peer failed to start");
        var errors = process.StandardError.ReadToEndAsync();
        using MemoryStream output = new();
        var reading = process.StandardOutput.BaseStream.CopyToAsync(output);
        process.StandardInput.BaseStream.Write(input); process.StandardInput.Close();
        if (!process.WaitForExit(10000))
        {
            process.Kill(); process.WaitForExit(); throw new Exception("OpenSSL test peer timed out");
        }
        reading.GetAwaiter().GetResult(); errors.GetAwaiter().GetResult();
        if (process.ExitCode != 0 || output.Length > 1048576)
            throw new CryptographicException("OpenSSL synthetic fixture rejected input");
        return output.ToArray();
    }

    internal static byte[] Derive(ECDiffieHellman client, byte[] serverBlob)
    {
        if (OperatingSystem.IsWindows()) return NativeCngFixtures.Derive(client, serverBlob);
        if (serverBlob.Length != 72 || !serverBlob.AsSpan(0,8).SequenceEqual(new byte[]{0x45,0x43,0x4b,0x31,32,0,0,0}))
            throw new CryptographicException("Invalid synthetic P-256 blob");
        string folder = Path.Combine(AppContext.BaseDirectory,"openssl-"+Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(folder, UnixFileMode.UserRead | UnixFileMode.UserWrite | UnixFileMode.UserExecute);
        byte[] secret = client.ExportPkcs8PrivateKey();
        try
        {
            string key = Path.Combine(folder,"key.der"), peer = Path.Combine(folder,"peer.der");
            File.WriteAllBytes(key,secret);
            // Standard P-256 SubjectPublicKeyInfo with an uncompressed point.
            byte[] header = Convert.FromHexString("3059301306072A8648CE3D020106082A8648CE3D03010703420004");
            File.WriteAllBytes(peer,[..header,..serverBlob.AsSpan(8).ToArray()]);
            byte[] agreement = OpenSsl([],"pkeyutl","-derive","-inkey",key,"-keyform","DER",
                                        "-peerkey",peer,"-peerform","DER");
            try { return SHA1.HashData(agreement).AsSpan(0,16).ToArray(); }
            finally { CryptographicOperations.ZeroMemory(agreement); }
        }
        finally { CryptographicOperations.ZeroMemory(secret); Directory.Delete(folder,true); }
    }

    internal static byte[] Encrypt(byte[] input, byte[] key, byte[] iv) => OperatingSystem.IsWindows()
        ? NativeCngFixtures.Encrypt(input,key,iv) : OpenSsl(input,"enc","-aes-128-cbc","-K",Convert.ToHexString(key),"-iv",Convert.ToHexString(iv));
    internal static byte[] Decrypt(byte[] input, byte[] key, byte[] iv) => OperatingSystem.IsWindows()
        ? NativeCngFixtures.Decrypt(input,key,iv) : OpenSsl(input,"enc","-d","-aes-128-cbc","-K",Convert.ToHexString(key),"-iv",Convert.ToHexString(iv));
}
