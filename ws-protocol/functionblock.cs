// ws-protocol-Schema fuer den 'functionblock'-Namespace (ehemals ns03functionblock.fbs). Bare-Skalar-
// Arrays (bools/integers/floats/colors) haben keine direkte Entsprechung im Protokoll (Array-Elemente
// eines UniformPackedArrayField muessen ein deklarierter fester Typ sein) -- Workaround: je ein
// Einzelfeld-Element-Typ (z.B. BoolValue{Value:bool}), Zugriff dann als item.value statt eines nackten
// Werts.
using BestBinaryBuffers;

namespace functionblock;

[BinaryType]
public struct BoolValue
{
	public bool Value;
}

[BinaryType]
public struct IntValue
{
	public int Value;
}

[BinaryType]
public struct FloatValue
{
	public float Value;
}

[BinaryType]
public struct ColorValue
{
	public uint Value;
}

[BinaryMessage(MessageKind.Request)]
public class RequestFbdRun
{
}

[BinaryMessage(MessageKind.Response)]
public class ResponseFbdRun
{
}

[BinaryMessage(MessageKind.Request)]
public class RequestDebugData
{
}

[BinaryMessage(MessageKind.Response)]
public class ResponseDebugData
{
	public uint DebugInfoHash;
	public BoolValue[] Bools;
	public IntValue[] Integers;
	public FloatValue[] Floats;
	public ColorValue[] Colors;
}
