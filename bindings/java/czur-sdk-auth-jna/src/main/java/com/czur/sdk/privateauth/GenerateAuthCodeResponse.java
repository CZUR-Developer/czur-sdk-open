package com.czur.sdk.privateauth;

import com.fasterxml.jackson.annotation.JsonProperty;

public final class GenerateAuthCodeResponse {
    private int code;
    private String message;

    @JsonProperty("auth_code")
    private String authCode;

    @JsonProperty("machine_code")
    private String machineCode;

    @JsonProperty("expires_date")
    private String expiresDate;

    @JsonProperty("auth_type")
    private int authType;

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

    public String getAuthCode() {
        return authCode;
    }

    public void setAuthCode(String authCode) {
        this.authCode = authCode;
    }

    public String getMachineCode() {
        return machineCode;
    }

    public void setMachineCode(String machineCode) {
        this.machineCode = machineCode;
    }

    public String getExpiresDate() {
        return expiresDate;
    }

    public void setExpiresDate(String expiresDate) {
        this.expiresDate = expiresDate;
    }

    public int getAuthType() {
        return authType;
    }

    public void setAuthType(int authType) {
        this.authType = authType;
    }
}
