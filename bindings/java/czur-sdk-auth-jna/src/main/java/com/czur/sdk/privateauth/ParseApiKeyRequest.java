package com.czur.sdk.privateauth;

import com.fasterxml.jackson.annotation.JsonProperty;

public final class ParseApiKeyRequest {
    @JsonProperty("api_key")
    private String apiKey;

    @JsonProperty("now_ts")
    private long nowTs;

    public ParseApiKeyRequest() {
    }

    public ParseApiKeyRequest(String apiKey, long nowTs) {
        this.apiKey = apiKey;
        this.nowTs = nowTs;
    }

    public String getApiKey() {
        return apiKey;
    }

    public void setApiKey(String apiKey) {
        this.apiKey = apiKey;
    }

    public long getNowTs() {
        return nowTs;
    }

    public void setNowTs(long nowTs) {
        this.nowTs = nowTs;
    }
}
