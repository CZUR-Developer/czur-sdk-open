package com.czur.sdk.privateauth;

public final class NativeResponseException extends RuntimeException {
    private final int code;
    private final String responseJson;

    public NativeResponseException(int code, String message) {
        this(code, message, null, null);
    }

    public NativeResponseException(int code, String message, Throwable cause) {
        this(code, message, null, cause);
    }

    public NativeResponseException(int code, String message, String responseJson) {
        this(code, message, responseJson, null);
    }

    private NativeResponseException(int code, String message, String responseJson, Throwable cause) {
        super(message, cause);
        this.code = code;
        this.responseJson = responseJson;
    }

    public int getCode() {
        return code;
    }

    public String getResponseJson() {
        return responseJson;
    }
}
