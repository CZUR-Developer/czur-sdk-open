package com.czur.sdk.privateauth;

import com.fasterxml.jackson.annotation.JsonProperty;

import java.util.List;

public final class ParseApiKeyResponse {
    private int code;
    private String message;

    @JsonProperty("is_valid")
    private boolean valid;

    @JsonProperty("key_id")
    private String keyId;

    private String nonce;

    @JsonProperty("token_digest")
    private String tokenDigest;

    private String tier;

    @JsonProperty("tier_code")
    private int tierCode;

    @JsonProperty("licensed_account_type")
    private String licensedAccountType;

    @JsonProperty("licensed_account_type_code")
    private int licensedAccountTypeCode;

    @JsonProperty("license_mode")
    private String licenseMode;

    @JsonProperty("host_auth_mode")
    private String hostAuthMode;

    @JsonProperty("entitlement_state")
    private String entitlementState;

    @JsonProperty("commercial_authorized")
    private boolean commercialAuthorized;

    @JsonProperty("issued_at")
    private long issuedAt;

    @JsonProperty("expires_at")
    private long expiresAt;

    @JsonProperty("device_scope")
    private List<DeviceGrant> deviceScope;

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

    public boolean isValid() {
        return valid;
    }

    public void setValid(boolean valid) {
        this.valid = valid;
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

    public String getTokenDigest() {
        return tokenDigest;
    }

    public void setTokenDigest(String tokenDigest) {
        this.tokenDigest = tokenDigest;
    }

    public String getTier() {
        return tier;
    }

    public void setTier(String tier) {
        this.tier = tier;
    }

    public int getTierCode() {
        return tierCode;
    }

    public void setTierCode(int tierCode) {
        this.tierCode = tierCode;
    }

    public String getLicensedAccountType() {
        return licensedAccountType;
    }

    public void setLicensedAccountType(String licensedAccountType) {
        this.licensedAccountType = licensedAccountType;
    }

    public int getLicensedAccountTypeCode() {
        return licensedAccountTypeCode;
    }

    public void setLicensedAccountTypeCode(int licensedAccountTypeCode) {
        this.licensedAccountTypeCode = licensedAccountTypeCode;
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

    public String getEntitlementState() {
        return entitlementState;
    }

    public void setEntitlementState(String entitlementState) {
        this.entitlementState = entitlementState;
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

    public List<DeviceGrant> getDeviceScope() {
        return deviceScope;
    }

    public void setDeviceScope(List<DeviceGrant> deviceScope) {
        this.deviceScope = deviceScope;
    }
}
