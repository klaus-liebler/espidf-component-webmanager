// ws-protocol-Schema fuer den 'chatbot'-Namespace (ehemals ns05chatbot.fbs). Nur Strings -- keine
// neuen Konstrukte noetig.
using BestBinaryBuffers;

namespace chatbot;

[BinaryMessage(MessageKind.Request)]
public class RequestChat
{
	[BinaryMaxEncodedByteLength(255)] public string Text;
}

[BinaryMessage(MessageKind.Response)]
public class ResponseChat
{
	[BinaryMaxEncodedByteLength(511)] public string Text;
}
