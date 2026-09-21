namespace SdkOpen.TestClient.WinForms.Controls;

public static class SplitterDistanceCalculator
{
    public static int? Calculate(int availableSize, int preferredDistance, int panel1Minimum, int panel2Minimum, int splitterWidth)
    {
        // 控件尚未完成布局时空间可能不足；此时返回 null，交给 SizeChanged 再尝试。
        // Layout space may be insufficient before initialization; return null and retry on SizeChanged.
        if (availableSize < panel1Minimum + panel2Minimum + splitterWidth) return null;
        // 将设计稿中的偏好位置限制在两个面板最小尺寸允许的安全范围内。
        // Clamp the preferred design position to the safe range allowed by both panel minimums.
        return Math.Clamp(preferredDistance, panel1Minimum, availableSize - panel2Minimum - splitterWidth);
    }
}
