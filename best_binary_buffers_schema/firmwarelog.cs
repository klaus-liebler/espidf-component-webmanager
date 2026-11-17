// ws-protocol-Schema fuer den 'firmwarelog'-Namespace: das ESP-IDF-Log (ESP_LOGx) im Browser. Opt-in:
// nur Firmware, die das FirmwareLogPlugin (cpp/webmanager_plugins/firmwarelog_plugin.hh) anmeldet,
// beantwortet diese Nachrichten. Der Browser fordert mit RequestLogHistory an; danach schickt das Geraet
// die noch gepufferten Zeilen und jede neue als NotifyLogMessage (bis zur naechsten Verbindung).
using BestBinaryBuffers;

namespace firmwarelog;

/// Wie esp_log_level_t.
[BinaryType]
public enum LogLevel : byte
{
	None = 0,
	Error = 1,
	Warn = 2,
	Info = 3,
	Debug = 4,
	Verbose = 5,
}

/// Eine Log-Zeile. Sequence zaehlt ab dem Start fortlaufend hoch: Luecken = verlorene Zeilen
/// (Puffer voll, bevor sie gesendet werden konnten).
[BinaryMessage(MessageKind.Event)]
public class NotifyLogMessage
{
	public uint Sequence;
	public LogLevel Level;
	/// Zeitstempel des Logs (Millisekunden seit dem Start).
	public uint UptimeMs;
	[BinaryMaxEncodedByteLength(23)] public string Tag;
	[BinaryMaxEncodedByteLength(191)] public string Text;
}

/// Startet die Uebertragung: erst der Pufferinhalt, dann jede neue Zeile.
[BinaryMessage(MessageKind.Request)]
public class RequestLogHistory
{
}

[BinaryMessage(MessageKind.Response)]
public class ResponseLogHistory
{
	/// Aelteste noch gepufferte und naechste Sequenznummer.
	public uint OldestSequence;
	public uint NextSequence;
}

/// esp_log_level_set(Tag, Level); leerer Tag = alle ("*").
[BinaryMessage(MessageKind.Request)]
public class RequestSetLogLevel
{
	[BinaryMaxEncodedByteLength(23)] public string Tag;
	public LogLevel Level;
}

[BinaryMessage(MessageKind.Response)]
public class ResponseSetLogLevel
{
}
