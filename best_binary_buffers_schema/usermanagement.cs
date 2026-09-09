// Schema fuer die Nutzerverwaltung (Mehrbenutzer + Rollen, Abloesung des bisherigen einzelnen fest
// kodierten Admin-Logins). Kein Array-Container-Typ ("UserDatabase") noetig: jeder Nutzer wird als
// eigene Datei unter /spiffs/users/<username>.bin abgelegt (Verzeichnislisting liefert "alle Nutzer"),
// dieselbe Bequemlichkeit wie ein serialisierbarer Wire-Message-Typ (Encode/Decode inkl. Header), nur
// dass die encodierten Bytes statt ueber den Websocket in eine Datei geschrieben werden. Ein Wrapper
// mit User[]-Array waere hier unpassend gewesen, weil die Schema-DSL laengenpraefixierte Arrays nur
// fuer Message-Felder mit fixed-size Elementen (UniformPackedArrayField) oder string[] erlaubt -- ein
// UserRecord hat aber mehrere string-Felder und ist damit selbst nicht fixed-size.
using BestBinaryBuffers;

namespace usermanagement;

/// Bitmaske -- ein Nutzer kann mehrere Rollen gleichzeitig haben (Roles-Feld ist eine ODER-Verknuepfung
/// dieser Werte).
[BinaryType]
public enum Role : byte
{
	Admin = 1,
	Operator = 2,
	Viewer = 4,
}

/// Ein Nutzerdatensatz. Persistiert als eigene Datei pro Nutzer (s.o.), niemals ueber den Websocket
/// verschickt -- MessageKind.Event nur, um die generierten Encode()/Decode()-Helfer (inkl.
/// Format-Kennung durch den 4-Byte-Header) fuer die Dateiablage wiederzuverwenden.
[BinaryMessage(MessageKind.Event)]
public class UserRecord
{
	[BinaryMaxEncodedByteLength(32)] public string Username;
	[BinaryMaxEncodedByteLength(32)] public string Salt;         // hex-kodiert, 16 zufaellige Bytes (esp_fill_random)
	[BinaryMaxEncodedByteLength(64)] public string PasswordHash; // hex-kodiert, SHA-256(salt || password)
	public byte Roles;          // Bitmaske aus Role
	public uint Epoch;          // hochgezaehlt bei Passwortaenderung -> invalidiert alle bestehenden Sessions dieses Nutzers
}
