// ws-protocol-Schema fuer den 'scheduler'-Namespace (ehemals ns08scheduler.fbs, Flatbuffers).
using BestBinaryBuffers;

namespace scheduler;

/// 84 Byte Rohdaten: ein Bit je 15-Minuten-Slot einer Woche (ehemals Flatbuffers-struct mit v:[uint8:84]).
[BinaryType]
public struct OneWeekIn15MinutesData
{
	[BinaryCount(84)] public byte[] V;
}

[BinaryUnion]
public interface IScheduleVariant
{
}

/// Platzhalter-Variante ohne eigene Daten (ehemals leere Flatbuffers-table).
[BinaryType]
public class Predefined : IScheduleVariant
{
}

/// Fester Wochenplan.
[BinaryType]
public class OneWeekIn15Minutes : IScheduleVariant
{
	public OneWeekIn15MinutesData Data;
}

/// Zufaellige Zeit relativ zu Sonnenauf-/-untergang.
[BinaryType]
public class SunRandom : IScheduleVariant
{
	public ushort OffsetMinutes;
	public ushort RandomMinutes;
}

// Eigener Union-Typ (nicht IScheduleVariant direkt) fuer den einzelnen "payload"-Nachrichten-Fall unten
// -- dort ist Schedule selbst (inkl. name) das getaggte Element, nicht eine der drei Schedule-Varianten.
[BinaryUnion]
public interface IScheduleContainer
{
}

/// Ein benannter Zeitplan mit einer von drei moeglichen Auspraegungen (ehemals Flatbuffers-union
/// 'uSchedule' als Feld). Selbst eine Class (nicht Struct), weil sie einen String traegt -- das
/// abschliessende polymorphe Feld "schedule" ist deshalb ein einzelnes IScheduleVariant-Feld (kein Array).
[BinaryType]
public class Schedule : IScheduleContainer
{
	public string Name;
	public IScheduleVariant Schedule;
}

[BinaryType]
public enum ScheduleType : byte
{
	PREDEFINED = 0,
	ONE_WEEK_IN_15_MINUTES = 1,
	SUN_RANDOM = 2,
}

[BinaryUnion]
public interface ISchedulerListItem
{
}

/// Eintrag der Zeitplan-Uebersichtsliste (ehemals ResponseSchedulerListItem).
[BinaryType]
public class SchedulerListItem : ISchedulerListItem
{
	public string Name;
	public ScheduleType Type;
}

[BinaryMessage(MessageKind.Request)]
public class RequestSchedulerList
{
}

[BinaryMessage(MessageKind.Response)]
public class ResponseSchedulerList
{
	public ISchedulerListItem[] Items;
}

[BinaryMessage(MessageKind.Request)]
public class RequestSchedulerOpen
{
	public string Name;
	public ScheduleType Type;
}

[BinaryMessage(MessageKind.Response)]
public class ResponseSchedulerOpen
{
	public IScheduleContainer Payload;
}

[BinaryMessage(MessageKind.Request)]
public class RequestSchedulerSave
{
	public IScheduleContainer Payload;
}

[BinaryMessage(MessageKind.Response)]
public class ResponseSchedulerSave
{
	public string Name;
}

/// Kein Response-Gegenstueck im urspruenglichen Flatbuffers-Schema -- Request hier trotzdem als Kind
/// gewaehlt (konsistent mit der Herkunft aus der 'Requests'-Union), requestId bleibt ungenutzt.
[BinaryMessage(MessageKind.Request)]
public class RequestSchedulerRename
{
	public string OldName;
	public string NewName;
}

/// Kein Response-Gegenstueck im urspruenglichen Flatbuffers-Schema, s. Kommentar bei RequestSchedulerRename.
[BinaryMessage(MessageKind.Request)]
public class RequestSchedulerDelete
{
	public string Name;
}
