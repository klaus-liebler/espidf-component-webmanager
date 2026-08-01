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

[BinaryType]
public class StringSettingWrapper : ISettingWrapper
{
	public string SettingKey;
	public string Value;
}

[BinaryType]
public class IntegerSettingWrapper : ISettingWrapper
{
	public string SettingKey;
	public int Value;
}

[BinaryType]
public class BooleanSettingWrapper : ISettingWrapper
{
	public string SettingKey;
	public bool Value;
}

[BinaryType]
public class EnumSettingWrapper : ISettingWrapper
{
	public string SettingKey;
	public int Value;
}

[BinaryMessage(MessageKind.Request)]
public class RequestGetUserSettings
{
	public string GroupKey;
}

[BinaryMessage(MessageKind.Response)]
public class ResponseGetUserSettings
{
	public string GroupKey;
	public ISettingWrapper[] Settings;
}

[BinaryMessage(MessageKind.Request)]
public class RequestSetUserSettings
{
	public string GroupKey;
	public ISettingWrapper[] Settings;
}

[BinaryMessage(MessageKind.Response)]
public class ResponseSetUserSettings
{
	public string GroupKey;
	public string[] SettingKeys;
}
