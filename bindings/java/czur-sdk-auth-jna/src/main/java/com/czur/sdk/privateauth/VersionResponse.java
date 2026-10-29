package com.czur.sdk.privateauth;

import com.fasterxml.jackson.annotation.JsonProperty;

public final class VersionResponse {
    private int code;
    private String message;
    private String name;

    @JsonProperty("abi_version")
    private int abiVersion;

    @JsonProperty("api_key_version")
    private String apiKeyVersion;

    public int getCode() {
        return code;
    }

    public void setCode(int code) {
        this.code = code;
    }

    public String getMessage() {
        return message;
    }

    public void setMessage(String message) {
        this.message = message;
    }

    public String getName() {
        return name;
    }

    public void setName(String name) {
        this.name = name;
    }

    public int getAbiVersion() {
        return abiVersion;
    }

    public void setAbiVersion(int abiVersion) {
        this.abiVersion = abiVersion;
    }

    public String getApiKeyVersion() {
        return apiKeyVersion;
    }

    public void setApiKeyVersion(String apiKeyVersion) {
        this.apiKeyVersion = apiKeyVersion;
    }
}
