package com.czur.sdk.privateauth;

import com.fasterxml.jackson.annotation.JsonProperty;

public final class GenerateAuthCodeRequest {
    @JsonProperty("machine_code")
    private String machineCode;

    @JsonProperty("expires_date")
    private String expiresDate;

    @JsonProperty("auth_type")
    private int authType;

    public GenerateAuthCodeRequest() {
    }

    public GenerateAuthCodeRequest(String machineCode, String expiresDate, int authType) {
        this.machineCode = machineCode;
        this.expiresDate = expiresDate;
        this.authType = authType;
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
