/*
 * tst_opcua_encode.c — 本地隔离测试：不连任何服务器，仅在本机用 vendored open62541
 * 复现客户端 ActivateSession 阶段发送的消息编码路径。
 *
 * 背景：TRACE 日志显示 SecureChannel(None) 打开成功、CreateSession 成功，但 ActivateSession
 * 在“发送请求”阶段（客户端本地编码，未到网络）返回 BadInternalError。经排除法定位到
 * 失败发生在 UA_MessageContext_encode 编码请求体——唯一区别于 CreateSession 的字段是
 * userIdentityToken（匿名 ExtensionObject）。本测试直接编码该 ExtensionObject 与整个
 * ActivateSessionRequest，若在本机即失败，则证明是 open62541 amalgamation/类型注册问题，
 * 与网络、服务器无关。
 */
#include <open62541.h>
#include <stdio.h>

static int check(const char* label, UA_StatusCode st) {
    printf("%-45s : %s (0x%08x)\n", label, UA_StatusCode_name(st), (unsigned)st);
    return st == UA_STATUSCODE_GOOD ? 0 : 1;
}

int main(void) {
    int fails = 0;

    /* 0) 匿名 token 类型的 binaryEncodingId 是否有效（0 表示类型未注册 → 编码必失败） */
    const UA_DataType* at = &UA_TYPES[UA_TYPES_ANONYMOUSIDENTITYTOKEN];
    printf("AnonymousIdentityToken binaryEncodingId: ns=%u id=%u typeKind=%u\n",
           at->binaryEncodingId.namespaceIndex,
           (unsigned)at->binaryEncodingId.identifier.numeric,
           (unsigned)at->typeKind);

    /* 1) 单独编码匿名 UserIdentityToken 的 ExtensionObject（客户端 activateSessionAsync 构造法） */
    UA_AnonymousIdentityToken anon;
    UA_AnonymousIdentityToken_init(&anon);
    anon.policyId = UA_STRING_ALLOC("open62541-anonymous-policy-none");

    UA_ExtensionObject eo;
    UA_ExtensionObject_setValueNoDelete(&eo, &anon,
                                        &UA_TYPES[UA_TYPES_ANONYMOUSIDENTITYTOKEN]);

    UA_ByteString buf1 = UA_BYTESTRING_NULL;   /* length 0 → 自动分配 */
    UA_StatusCode st1 = UA_encodeBinary(&eo, &UA_TYPES[UA_TYPES_EXTENSIONOBJECT], &buf1, NULL);
    fails += check("encode userIdentityToken ExtensionObject", st1);
    if (st1 == UA_STATUSCODE_GOOD)
        printf("    -> encoded %u bytes\n", (unsigned)buf1.length);
    UA_ByteString_clear(&buf1);

    /* 2) 编码整个 ActivateSessionRequest（含匿名 token），与客户端实际发送内容等价 */
    UA_ActivateSessionRequest req;
    UA_ActivateSessionRequest_init(&req);
    UA_AnonymousIdentityToken anon2;
    UA_AnonymousIdentityToken_init(&anon2);
    anon2.policyId = UA_STRING_ALLOC("open62541-anonymous-policy-none");
    UA_ExtensionObject_setValueNoDelete(&req.userIdentityToken, &anon2,
                                        &UA_TYPES[UA_TYPES_ANONYMOUSIDENTITYTOKEN]);

    UA_ByteString buf2 = UA_BYTESTRING_NULL;
    UA_StatusCode st2 = UA_encodeBinary(&req, &UA_TYPES[UA_TYPES_ACTIVATESESSIONREQUEST], &buf2, NULL);
    fails += check("encode ActivateSessionRequest", st2);
    if (st2 == UA_STATUSCODE_GOOD)
        printf("    -> encoded %u bytes\n", (unsigned)buf2.length);
    UA_ByteString_clear(&buf2);

    /* 3) 对照组：编码 ReadRequest（不含 ExtensionObject），验证编码器基础可用 */
    UA_ReadRequest rd;
    UA_ReadRequest_init(&rd);
    UA_ByteString buf3 = UA_BYTESTRING_NULL;
    UA_StatusCode st3 = UA_encodeBinary(&rd, &UA_TYPES[UA_TYPES_READREQUEST], &buf3, NULL);
    fails += check("encode ReadRequest (control)", st3);
    UA_ByteString_clear(&buf3);

    UA_String_clear(&anon.policyId);
    UA_String_clear(&anon2.policyId);

    printf("\n结果：%s\n", fails == 0 ? "全部编码成功（库本身无问题）"
                                      : "存在编码失败（问题在 open62541 库/类型注册，与网络无关）");
    return fails;
}
