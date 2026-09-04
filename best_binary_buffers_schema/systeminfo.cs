// ws-protocol-Schema fuer den 'systeminfo'-Namespace (ehemals ns02systeminfo.fbs). Testfall fuer
// int64/uint64-Feldunterstuetzung (secondsEpoch/secondsUptime) UND Mehrfachverwendung eines Structs
// (Mac6, 5x). partitions ist eine klassische einklassige Union-Liste, analog scheduler.SchedulerListItem.
using BestBinaryBuffers;

namespace systeminfo;

/// 6 Byte MAC-Adresse (ehemals Flatbuffers-struct mit v:[uint8:6]).
[BinaryType]
public struct Mac6
{
	[BinaryCount(6)] public byte[] V;
}

[BinaryUnion]
public interface IPartitionInfo
{
}

[BinaryType]
public class PartitionInfo : IPartitionInfo
{
	// Bounds aus ESP-IDF: esp_partition.h (label[17]) sowie esp_app_desc.h (project_name[32],
	// version[32], date[16], time[16]).
	[BinaryMaxEncodedByteLength(16)] public string Label;
	public byte Type;
	public byte Subtype;
	public uint Size;
	public sbyte OtaState;
	public bool Running;
	[BinaryMaxEncodedByteLength(31)] public string AppName;
	[BinaryMaxEncodedByteLength(31)] public string AppVersion;
	[BinaryMaxEncodedByteLength(15)] public string AppDate;
	[BinaryMaxEncodedByteLength(15)] public string AppTime;
}

[BinaryMessage(MessageKind.Request)]
public class RequestRestart
{
}

[BinaryMessage(MessageKind.Request)]
public class RequestSystemData
{
}

[BinaryMessage(MessageKind.Response)]
public class ResponseSystemData
{
	public long SecondsEpoch;
	public long SecondsUptime;
	public uint FreeHeap;
	public Mac6 MacAddressWifiSta;
	public Mac6 MacAddressWifiSoftap;
	public Mac6 MacAddressBt;
	public Mac6 MacAddressEth;
	public Mac6 MacAddressIeee802154;
	public uint ChipModel;
	public byte ChipFeatures;
	public ushort ChipRevision;
	public byte ChipCores;
	public float ChipTemperature;
	[BinaryMaxItemCount(16)] public IPartitionInfo[] Partitions;
}
