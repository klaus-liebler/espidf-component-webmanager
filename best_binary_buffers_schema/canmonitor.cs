// ws-protocol-Schema fuer den 'canmonitor'-Namespace (ehemals ns06canmonitor.fbs). NotifyCanMessage war
// im Flatbuffers-Schema eine eigenstaendige Tabelle ohne RequestWrapper/ResponseWrapper -- passt am
// ehesten zu MessageKind.Event (Server-Push, kein Request dahinter, analog zu system.LogMessage).
using BestBinaryBuffers;

namespace canmonitor;

/// 8 Byte CAN-Nutzdaten (ehemals Flatbuffers-struct mit data:[uint8:8]).
[BinaryType]
public struct CanData
{
	[BinaryCount(8)] public byte[] Data;
}

[BinaryMessage(MessageKind.Event)]
public class NotifyCanMessage
{
	public uint MessageId;
	public CanData Data;
	public byte DataLen;
}
