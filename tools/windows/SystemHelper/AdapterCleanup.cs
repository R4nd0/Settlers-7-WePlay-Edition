using System.ComponentModel;
using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.RegularExpressions;
using Microsoft.Win32;
using Microsoft.Win32.SafeHandles;

// Remove only the exact randomly named Wintun adapter owned by a stopped room.
// This never deletes the Wintun driver or a physical/unrelated network device.
internal static class AdapterCleanup
{
    [StructLayout(LayoutKind.Sequential)]
    private struct Device {public uint Size;public Guid ClassGuid;public uint DevInst;public IntPtr Reserved;}
    [StructLayout(LayoutKind.Sequential)]
    private struct RemoveParameters {public uint HeaderSize;public uint Function;public uint Scope;public uint Profile;}
    [DllImport("setupapi.dll",CharSet=CharSet.Unicode,SetLastError=true)]
    private static extern IntPtr SetupDiGetClassDevsW(ref Guid guid,string? enumerator,IntPtr parent,uint flags);
    [DllImport("setupapi.dll",SetLastError=true)]
    private static extern bool SetupDiEnumDeviceInfo(IntPtr set,uint index,ref Device device);
    [DllImport("setupapi.dll",SetLastError=true)]
    private static extern IntPtr SetupDiOpenDevRegKey(IntPtr set,ref Device device,uint scope,uint profile,uint type,uint access);
    [DllImport("setupapi.dll",CharSet=CharSet.Unicode,SetLastError=true)]
    private static extern bool SetupDiGetDeviceRegistryPropertyW(IntPtr set,ref Device device,uint property,out uint type,byte[] buffer,uint size,out uint required);
    [DllImport("setupapi.dll",SetLastError=true)]
    private static extern bool SetupDiSetClassInstallParamsW(IntPtr set,ref Device device,ref RemoveParameters parameters,uint size);
    [DllImport("setupapi.dll",SetLastError=true)]
    private static extern bool SetupDiCallClassInstaller(uint function,IntPtr set,ref Device device);
    [DllImport("setupapi.dll",SetLastError=true)]
    private static extern bool SetupDiDestroyDeviceInfoList(IntPtr set);

    internal static void Remove(string name,string address)
    {
        if(!Regex.IsMatch(name,"\\AWePlay-[0-9a-f]{8}\\z")||!IPAddress.TryParse(address,out var ip)||
           ip.AddressFamily!=AddressFamily.InterNetwork||ip.ToString()!=address)throw new InvalidDataException("helper_invalid");
        var bytes=ip.GetAddressBytes();
        if(!(bytes[0]==10||bytes[0]==172&&bytes[1]>=16&&bytes[1]<=31||bytes[0]==192&&bytes[1]==168))throw new InvalidDataException("helper_invalid");
        var adapters=NetworkInterface.GetAllNetworkInterfaces().Where(x=>x.Name==name).ToArray();
        if(adapters.Length==0)return;
        if(adapters.Length!=1||!Guid.TryParse(adapters[0].Id,out Guid adapterId))throw new InvalidDataException("helper_overlay_changed");
        var addresses=adapters[0].GetIPProperties().UnicastAddresses.Where(x=>x.Address.AddressFamily==AddressFamily.InterNetwork).ToArray();
        if(addresses.Length>1||addresses.Length==1&&addresses[0].Address.ToString()!=address)throw new InvalidDataException("helper_overlay_changed");
        Guid networkClass=new("4d36e972-e325-11ce-bfc1-08002be10318");
        // Include non-present devices: a stopped Wintun session can leave its
        // installed, disconnected adapter visible without DIGCF_PRESENT.
        IntPtr set=SetupDiGetClassDevsW(ref networkClass,null,IntPtr.Zero,0);
        if(set==new IntPtr(-1))throw new Win32Exception(Marshal.GetLastWin32Error());
        try
        {
            for(uint index=0;;index++)
            {
                var device=new Device{Size=(uint)Marshal.SizeOf<Device>()};
                if(!SetupDiEnumDeviceInfo(set,index,ref device))
                {if(Marshal.GetLastWin32Error()==259)break;throw new Win32Exception(Marshal.GetLastWin32Error());}
                IntPtr raw=SetupDiOpenDevRegKey(set,ref device,1,0,2,1);
                if(raw==new IntPtr(-1))continue;
                using var handle=new SafeRegistryHandle(raw,true);
                using var registry=RegistryKey.FromHandle(handle);
                if(registry.GetValue("NetCfgInstanceId") is not string identity||!Guid.TryParse(identity,out var id)||id!=adapterId)continue;
                var buffer=new byte[1024];
                // SPDRP_SERVICE (setupapi.h), not the adjacent reserved property.
                if(!SetupDiGetDeviceRegistryPropertyW(set,ref device,4,out _,buffer,(uint)buffer.Length,out uint required)||required>buffer.Length)
                    throw new InvalidDataException("helper_overlay_driver_changed");
                string service=Encoding.Unicode.GetString(buffer,0,(int)required).TrimEnd('\0');
                if(!service.Equals("wintun",StringComparison.OrdinalIgnoreCase))throw new InvalidDataException("helper_overlay_driver_changed");
                var remove=new RemoveParameters{HeaderSize=8,Function=5,Scope=1,Profile=0};
                if(!SetupDiSetClassInstallParamsW(set,ref device,ref remove,(uint)Marshal.SizeOf<RemoveParameters>())||!SetupDiCallClassInstaller(5,set,ref device))
                    throw new Win32Exception(Marshal.GetLastWin32Error());
                return;
            }
            throw new InvalidDataException("helper_overlay_identity_changed");
        }
        finally{SetupDiDestroyDeviceInfoList(set);}
    }
}
