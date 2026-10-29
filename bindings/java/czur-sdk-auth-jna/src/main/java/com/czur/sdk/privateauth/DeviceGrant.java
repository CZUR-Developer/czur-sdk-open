package com.czur.sdk.privateauth;

public final class DeviceGrant {
    private int vid;
    private int pid;

    public DeviceGrant() {
    }

    public DeviceGrant(int vid, int pid) {
        this.vid = vid;
        this.pid = pid;
    }

    public int getVid() {
        return vid;
    }

    public void setVid(int vid) {
        this.vid = vid;
    }

    public int getPid() {
        return pid;
    }

    public void setPid(int pid) {
        this.pid = pid;
    }
}
