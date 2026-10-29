package com.czur.sdk.privateauth;

public interface AuthCodeGenerator {
    GenerateAuthCodeResponse generateAuthCode(GenerateAuthCodeRequest request);
}
