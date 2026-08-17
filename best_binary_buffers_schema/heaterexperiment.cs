// ws-protocol-Schema fuer den 'heaterexperiment'-Namespace (ehemals ns04heaterexperiment.fbs).
using BestBinaryBuffers;

namespace heaterexperiment;

[BinaryType]
public enum Mode : byte
{
	FUNCTION_BLOCK = 0,
	OPEN_LOOP = 1,
	CLOSED_LOOP = 2,
}

[BinaryMessage(MessageKind.Request)]
public class RequestHeater
{
	public Mode Mode;
	public float HeaterPowerPercent;
	public float SetpointTemperatureDegrees;
	public float FanSpeedPercent;
	public float Kp;
	public float Tn;
	public float Tv;
	public float HeaterPowerWorkingPointPercent;
	public bool RegulatorReset;
}

[BinaryMessage(MessageKind.Response)]
public class ResponseHeater
{
	public float SetpointTemperatureDegrees;
	public float ActualTemperatureDegrees;
	public float HeaterPowerPercent;
	public float FanSpeedPercent;
}
