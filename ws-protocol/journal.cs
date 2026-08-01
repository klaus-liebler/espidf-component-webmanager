// ws-protocol-Schema fuer den 'journal'-Namespace (ehemals ns10journal.fbs). journalItems ist eine
// klassische einklassige Union-Liste (getaggt, s. BestBinaryBuffers/README.md). lastMessageTimestamp:
// uint64 in einer Class (nicht nur direkt in einer Message wie systeminfo.ResponseSystemData) --
// zusaetzliche Absicherung der int64/uint64-Unterstuetzung.
using BestBinaryBuffers;

namespace journal;

[BinaryUnion]
public interface IJournalItem
{
}

[BinaryType]
public class JournalItem : IJournalItem
{
	public ulong LastMessageTimestamp;
	public uint MessageCode;
	public string MessageString;
	public uint MessageData;
	public uint MessageCount;
}

[BinaryMessage(MessageKind.Request)]
public class RequestJournal
{
}

[BinaryMessage(MessageKind.Response)]
public class ResponseJournal
{
	public IJournalItem[] JournalItems;
}
