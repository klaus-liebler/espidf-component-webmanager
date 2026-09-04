// ws-protocol-Schema fuer den 'wifimanager'-Namespace (ehemals ns01wifimanager.fbs). accesspoints ist
// eine klassische einklassige Union-Liste (analog scheduler.SchedulerListItem, systeminfo.PartitionInfo).
using BestBinaryBuffers;

namespace wifimanager;

[BinaryUnion]
public interface IAccessPoint
{
}

[BinaryType]
public class AccessPoint : IAccessPoint
{
	// 32 = ssid[32] aus esp_wifi_types_generic.h (IEEE-802.11-SSID-Obergrenze).
	[BinaryMaxEncodedByteLength(32)] public string Ssid;
	public int PrimaryChannel;
	public int Rssi;
	public int AuthMode;
}

[BinaryMessage(MessageKind.Request)]
public class RequestNetworkInformation
{
	public bool ForceNewSearch;
}

[BinaryMessage(MessageKind.Response)]
public class ResponseNetworkInformation
{
	[BinaryMaxEncodedByteLength(32)] public string Hostname;
	[BinaryMaxEncodedByteLength(32)] public string SsidAp;
	// 63 = WPA2-PSK-Maximallaenge laut Standard (password[64] in esp_wifi_types_generic.h).
	[BinaryMaxEncodedByteLength(63)] public string PasswordAp;
	public uint IpAp;
	public bool IsConnectedSta;
	[BinaryMaxEncodedByteLength(32)] public string SsidSta;
	public uint IpSta;
	public uint NetmaskSta;
	public uint GatewaySta;
	public sbyte RssiSta;
	// 8 = MAX_AP_NUM aus webmanager_constants.hh.
	[BinaryMaxItemCount(8)] public IAccessPoint[] Accesspoints;
}

[BinaryMessage(MessageKind.Request)]
public class RequestWifiConnect
{
	[BinaryMaxEncodedByteLength(32)] public string Ssid;
	[BinaryMaxEncodedByteLength(63)] public string Password;
}

[BinaryMessage(MessageKind.Response)]
public class ResponseWifiConnect
{
	public bool Success;
	[BinaryMaxEncodedByteLength(32)] public string Ssid;
	public uint Ip;
	public uint Netmask;
	public uint Gateway;
	public sbyte Rssi;
}

[BinaryMessage(MessageKind.Request)]
public class RequestWifiDisconnect
{
}

[BinaryMessage(MessageKind.Response)]
public class ResponseWifiDisconnect
{
}
