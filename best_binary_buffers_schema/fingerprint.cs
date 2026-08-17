// ws-protocol-Schema fuer den 'fingerprint'-Namespace (ehemals ns07fingerprint.fbs).
// NotifyEnrollNewFinger/NotifyFingerDetected waren im Flatbuffers-Schema Teil der 'Responses'-Union
// (nicht wie canmonitor.NotifyCanMessage ausserhalb beider Unions) -- deshalb hier
// MessageKind.Response statt Event, um das urspruengliche Wire-Dispatch-Verhalten zu erhalten.
// ResponseFingers.scheduleNames nutzt (anders als noch im JSON-Schema, das dafuer den
// Einzelfeld-Wrapper "StringValue" brauchte) direkt ein natives string[] (UniformVariableArrayField) --
// das loest nebenbei auch, dass "scheduleNames" nicht mehr Teil des trailing-Polymorphic-Blocks ist und
// vor "fingers" stehen darf (BestBinaryBuffers erlaubt nur EIN trailing-polymorphes Feld pro Message).
using BestBinaryBuffers;

namespace fingerprint;

/// 32 Byte Rohdaten (ehemals Flatbuffers-struct mit v:[uint8:32]).
[BinaryType]
public struct Uint8x32
{
	[BinaryCount(32)] public byte[] V;
}

[BinaryUnion]
public interface IFinger
{
}

[BinaryType]
public class Finger : IFinger
{
	public string Name;
	public ushort Index;
	public string ScheduleName;
	public ushort ActionIndex;
}

[BinaryMessage(MessageKind.Request)]
public class RequestFingerActionManually
{
	public ushort FingerIndex;
	public ushort ActionIndex;
}

[BinaryMessage(MessageKind.Response)]
public class ResponseFingerActionManually
{
}

[BinaryMessage(MessageKind.Request)]
public class RequestEnrollNewFinger
{
	public string Name;
}

[BinaryMessage(MessageKind.Response)]
public class ResponseEnrollNewFinger
{
	public ushort Errorcode;
}

/// Server-Push waehrend eines laufenden Enroll-Vorgangs -- Teil der urspruenglichen 'Responses'-Union.
[BinaryMessage(MessageKind.Response)]
public class NotifyEnrollNewFinger
{
	public string Name;
	public ushort Index;
	public byte Step;
	public ushort Errorcode;
}

/// Server-Push bei erkanntem Finger -- Teil der urspruenglichen 'Responses'-Union.
[BinaryMessage(MessageKind.Response)]
public class NotifyFingerDetected
{
	public ushort Errorcode;
	public ushort Index;
	public byte Score;
}

[BinaryMessage(MessageKind.Request)]
public class RequestDeleteFinger
{
	public string Name;
}

[BinaryMessage(MessageKind.Response)]
public class ResponseDeleteFinger
{
	public ushort Errorcode;
	public string Name;
}

[BinaryMessage(MessageKind.Request)]
public class RequestStoreFingerAction
{
	public ushort FingerIndex;
	public ushort ActionIndex;
}

[BinaryMessage(MessageKind.Response)]
public class ResponseStoreFingerAction
{
}

[BinaryMessage(MessageKind.Request)]
public class RequestStoreFingerSchedule
{
	public ushort FingerIndex;
	public string ScheduleName;
}

[BinaryMessage(MessageKind.Response)]
public class ResponseStoreFingerSchedule
{
}

[BinaryMessage(MessageKind.Request)]
public class RequestDeleteAllFingers
{
}

[BinaryMessage(MessageKind.Response)]
public class ResponseDeleteAllFingers
{
	public ushort Errorcode;
}

[BinaryMessage(MessageKind.Request)]
public class RequestCancelInstruction
{
}

[BinaryMessage(MessageKind.Response)]
public class ResponseCancelInstruction
{
	public ushort Errorcode;
}

[BinaryMessage(MessageKind.Request)]
public class RequestRenameFinger
{
	public string OldName;
	public string NewName;
}

[BinaryMessage(MessageKind.Response)]
public class ResponseRenameFinger
{
	public ushort Errorcode;
}

[BinaryMessage(MessageKind.Request)]
public class RequestFingerprintSensorInfo
{
}

[BinaryMessage(MessageKind.Response)]
public class ResponseFingerprintSensorInfo
{
	public ushort Status;
	public ushort LibrarySizeMax;
	public ushort LibrarySizeUsed;
	public Uint8x32 LibraryUsedIndices;
	public byte SecurityLevel;
	public uint DeviceAddress;
	public byte DataPacketSizeCode;
	public byte BaudRateTimes9600;
	public string AlgVer;
	public string FwVer;
}

[BinaryMessage(MessageKind.Request)]
public class RequestFingers
{
}

[BinaryMessage(MessageKind.Response)]
public class ResponseFingers
{
	public string[] ScheduleNames;
	public IFinger[] Fingers;
}
