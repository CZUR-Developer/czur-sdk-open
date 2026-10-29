package com.czur.sdk.privateauth;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.sun.jna.Pointer;

import java.nio.charset.StandardCharsets;
import java.util.Objects;

public final class CzurSdkApiKeyIssuer implements ApiKeyIssuer, AuthCodeGenerator {
    private final CzurSdkPrivateAuthNative nativeApi;
    private final ObjectMapper objectMapper;

    public CzurSdkApiKeyIssuer(CzurSdkPrivateAuthNative nativeApi) {
        this(nativeApi, new ObjectMapper());
    }

    CzurSdkApiKeyIssuer(CzurSdkPrivateAuthNative nativeApi, ObjectMapper objectMapper) {
        this.nativeApi = Objects.requireNonNull(nativeApi, "nativeApi");
        this.objectMapper = Objects.requireNonNull(objectMapper, "objectMapper");
    }

    public static CzurSdkApiKeyIssuer createDefault() {
        return new CzurSdkApiKeyIssuer(CzurSdkPrivateAuthNative.loadDefault());
    }

    @Override
    public GenerateApiKeyResponse generate(GenerateApiKeyRequest request) {
        return call(request, NativeCall.GENERATE, GenerateApiKeyResponse.class);
    }

    @Override
    public ParseApiKeyResponse parse(ParseApiKeyRequest request) {
        return call(request, NativeCall.PARSE, ParseApiKeyResponse.class);
    }

    @Override
    public GenerateAuthCodeResponse generateAuthCode(GenerateAuthCodeRequest request) {
        return call(request, NativeCall.GENERATE_AUTH_CODE, GenerateAuthCodeResponse.class);
    }

    public VersionResponse version() {
        String responseJson = readAndFree(nativeApi.czur_sdk_private_version_json());
        return decodeSuccessfulResponse(responseJson, VersionResponse.class);
    }

    private <T> T call(Object request, NativeCall nativeCall, Class<T> responseType) {
        final String requestJson;
        try {
            requestJson = objectMapper.writeValueAsString(request);
        } catch (JsonProcessingException e) {
            throw new NativeResponseException(1002, "failed to encode request", e);
        }

        Pointer responsePointer;
        if (nativeCall == NativeCall.GENERATE) {
            responsePointer = nativeApi.czur_sdk_private_generate_api_key_json(requestJson);
        } else if (nativeCall == NativeCall.GENERATE_AUTH_CODE) {
            responsePointer = nativeApi.czur_sdk_private_generate_auth_code_json(requestJson);
        } else {
            responsePointer = nativeApi.czur_sdk_private_parse_api_key_json(requestJson);
        }
        return decodeSuccessfulResponse(readAndFree(responsePointer), responseType);
    }

    private String readAndFree(Pointer responsePointer) {
        if (responsePointer == null) {
            throw new NativeResponseException(1900, "native api returned null");
        }
        try {
            return responsePointer.getString(0, StandardCharsets.UTF_8.name());
        } finally {
            nativeApi.czur_sdk_private_free_string(responsePointer);
        }
    }

    private <T> T decodeSuccessfulResponse(String responseJson, Class<T> responseType) {
        try {
            JsonNode root = objectMapper.readTree(responseJson);
            int code = root.path("code").asInt(1900);
            String message = root.path("message").asText("");
            if (code != 0) {
                throw new NativeResponseException(code, message, responseJson);
            }
            return objectMapper.treeToValue(root, responseType);
        } catch (NativeResponseException e) {
            throw e;
        } catch (Exception e) {
            throw new NativeResponseException(1900, "failed to decode native response", e);
        }
    }

    private enum NativeCall {
        GENERATE,
        GENERATE_AUTH_CODE,
        PARSE
    }
}
