package com.czur.sdk.privateauth;

public interface ApiKeyIssuer {
    GenerateApiKeyResponse generate(GenerateApiKeyRequest request);

    ParseApiKeyResponse parse(ParseApiKeyRequest request);
}
