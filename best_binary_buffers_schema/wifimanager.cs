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
	public string Ssid;
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
	public string Hostname;
	public string SsidAp;
	public string PasswordAp;
	public uint IpAp;
	public bool IsConnectedSta;
	public string SsidSta;
	public uint IpSta;
	public uint NetmaskSta;
	public uint GatewaySta;
	public sbyte RssiSta;
	public IAccessPoint[] Accesspoints;
}

[BinaryMessage(MessageKind.Request)]
public class RequestWifiConnect
{
	public string Ssid;
	public string Password;
}

[BinaryMessage(MessageKind.Response)]
public class ResponseWifiConnect
{
	public bool Success;
	public string Ssid;
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
