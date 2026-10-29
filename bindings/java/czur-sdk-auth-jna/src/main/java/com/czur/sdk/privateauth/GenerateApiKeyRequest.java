package com.czur.sdk.privateauth;

import com.fasterxml.jackson.annotation.JsonProperty;

import java.util.ArrayList;
import java.util.List;

public final class GenerateApiKeyRequest {
    private String tier;

    @JsonProperty("license_mode")
    private String licenseMode = "offline_api_key";

    @JsonProperty("host_auth_mode")
    private String hostAuthMode = "none";

    @JsonProperty("device_scope")
    private List<DeviceGrant> deviceScope = new ArrayList<>();

    @JsonProperty("commercial_authorized")
    private boolean commercialAuthorized;

    @JsonProperty("issued_at")
    private long issuedAt;

    @JsonProperty("expires_at")
    private long expiresAt;

    @JsonProperty("key_id")
    private String keyId;

    private String nonce;

    public String getTier() {
        return tier;
    }

    public void setTier(String tier) {
        this.tier = tier;
    }

    public String getLicenseMode() {
        return licenseMode;
    }

    public void setLicenseMode(String licenseMode) {
        this.licenseMode = licenseMode;
    }

    public String getHostAuthMode() {
        return hostAuthMode;
    }

    public void setHostAuthMode(String hostAuthMode) {
        this.hostAuthMode = hostAuthMode;
    }

    public List<DeviceGrant> getDeviceScope() {
        return deviceScope;
    }

    public void setDeviceScope(List<DeviceGrant> deviceScope) {
        this.deviceScope = deviceScope;
    }

    public boolean isCommercialAuthorized() {
        return commercialAuthorized;
    }

    public void setCommercialAuthorized(boolean commercialAuthorized) {
        this.commercialAuthorized = commercialAuthorized;
    }

    public long getIssuedAt() {
        return issuedAt;
    }

    public void setIssuedAt(long issuedAt) {
        this.issuedAt = issuedAt;
    }

    public long getExpiresAt() {
        return expiresAt;
    }

    public void setExpiresAt(long expiresAt) {
        this.expiresAt = expiresAt;
    }

    public String getKeyId() {
        return keyId;
    }

    public void setKeyId(String keyId) {
        this.keyId = keyId;
    }

    public String getNonce() {
        return nonce;
    }

    public void setNonce(String nonce) {
        this.nonce = nonce;
    }
}
