package com.czur.sdk.privateauth;

import com.sun.jna.Library;
import com.sun.jna.Native;
import com.sun.jna.Pointer;

public interface CzurSdkPrivateAuthNative extends Library {
    String LIBRARY_PATH_PROPERTY = "czur.sdk.auth.library.path";
    String LIBRARY_PATH_ENV = "CZUR_SDK_AUTH_LIBRARY_PATH";
    String DEFAULT_LIBRARY_NAME = "sdk_private_auth_c_api";

    Pointer czur_sdk_private_generate_api_key_json(String requestJson);

    Pointer czur_sdk_private_parse_api_key_json(String requestJson);

    Pointer czur_sdk_private_generate_auth_code_json(String requestJson);

    Pointer czur_sdk_private_version_json();

    void czur_sdk_private_free_string(Pointer value);

    static CzurSdkPrivateAuthNative loadDefault() {
        String library = System.getProperty(LIBRARY_PATH_PROPERTY);
        if (library == null || library.trim().isEmpty()) {
            library = System.getenv(LIBRARY_PATH_ENV);
        }
        if (library == null || library.trim().isEmpty()) {
            library = DEFAULT_LIBRARY_NAME;
        }
        return Native.load(library, CzurSdkPrivateAuthNative.class);
    }
}
