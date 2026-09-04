// ws-protocol-Schema fuer den 'usersettings'-Namespace (ehemals ns09usersettings.fbs). Design bewusst
// abweichend vom Flatbuffers-Original: statt "SettingWrapper{setting_key, setting:Setting}" mit einer
// separaten 4-gliedrigen Union werden settingKey und value direkt in VIER eigenstaendigen Klassen
// zusammengefasst (StringSettingWrapper/IntegerSettingWrapper/BooleanSettingWrapper/EnumSettingWrapper),
// die als heterogenes Union-Array verwendet werden -- vermeidet eine unnoetige Verschachtelungsebene
// (Wrapper-um-Wrapper). settingKeys nutzt (anders als noch im JSON-Schema, das dafuer einen
// Einzelfeld-Wrapper "StringValue" brauchte) direkt ein natives string[] (UniformVariableArrayField).
using BestBinaryBuffers;

namespace usersettings;

[BinaryUnion]
public interface ISettingWrapper
{
}

// 32 = Bound aus usersettings_plugin.hh (Kommentar dort: "settingKey<=32+null", "Wert<=32+null").
[BinaryType]
public class StringSettingWrapper : ISettingWrapper
{
	[BinaryMaxEncodedByteLength(32)] public string SettingKey;
	[BinaryMaxEncodedByteLength(32)] public string Value;
}

[BinaryType]
public class IntegerSettingWrapper : ISettingWrapper
{
	[BinaryMaxEncodedByteLength(32)] public string SettingKey;
	public int Value;
}

[BinaryType]
public class BooleanSettingWrapper : ISettingWrapper
{
	[BinaryMaxEncodedByteLength(32)] public string SettingKey;
	public bool Value;
}

[BinaryType]
public class EnumSettingWrapper : ISettingWrapper
{
	[BinaryMaxEncodedByteLength(32)] public string SettingKey;
	public int Value;
}

[BinaryMessage(MessageKind.Request)]
public class RequestGetUserSettings
{
	// 15 = NVS_KEY_NAME_MAX_SIZE(16)-1 -- groupKey wird 1:1 als NVS-Namespace verwendet
	// (usersettings_plugin.hh: nvs_open_from_partition(partitionName, group->groupKey, ...)).
	[BinaryMaxEncodedByteLength(15)] public string GroupKey;
}

[BinaryMessage(MessageKind.Response)]
public class ResponseGetUserSettings
{
	[BinaryMaxEncodedByteLength(15)] public string GroupKey;
	// 32: passend zu usersettings_plugin.hh's Kommentar "bis zu 32 Settings".
	[BinaryMaxItemCount(32)] public ISettingWrapper[] Settings;
}

[BinaryMessage(MessageKind.Request)]
public class RequestSetUserSettings
{
	[BinaryMaxEncodedByteLength(15)] public string GroupKey;
	[BinaryMaxItemCount(32)] public ISettingWrapper[] Settings;
}

[BinaryMessage(MessageKind.Response)]
public class ResponseSetUserSettings
{
	[BinaryMaxEncodedByteLength(15)] public string GroupKey;
	[BinaryMaxItemCount(32)] [BinaryMaxEncodedByteLength(32)] public string[] SettingKeys;
}
