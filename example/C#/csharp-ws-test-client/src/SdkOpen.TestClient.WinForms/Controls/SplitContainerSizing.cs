namespace SdkOpen.TestClient.WinForms.Controls;

public static class SplitContainerSizing
{
    public static void ApplyAfterFirstValidLayout(SplitContainer splitContainer, int preferredDistance, int panel1Minimum, int panel2Minimum)
    {
        ArgumentNullException.ThrowIfNull(splitContainer);
        EventHandler? apply = null;
        apply = (_, _) =>
        {
            var availableSize = splitContainer.Orientation == Orientation.Vertical
                ? splitContainer.ClientSize.Width
                : splitContainer.ClientSize.Height;
            var distance = SplitterDistanceCalculator.Calculate(availableSize, preferredDistance, panel1Minimum, panel2Minimum, splitContainer.SplitterWidth);
            if (!distance.HasValue) return;
            // WinForms 构造期 ClientSize 通常还是 0；必须等首次有效布局后再设置最小尺寸和分隔条。
            // 否则 SplitContainer 会因“总空间小于两个最小面板”直接抛出 ArgumentException。
            // During construction ClientSize is usually zero; apply minimums and the splitter after the first valid layout.
            // Otherwise SplitContainer throws when the total space is smaller than both minimum panels.
            splitContainer.Panel1MinSize = panel1Minimum;
            splitContainer.Panel2MinSize = panel2Minimum;
            splitContainer.SplitterDistance = distance.Value;
            splitContainer.SizeChanged -= apply;
            splitContainer.Disposed -= apply;
        };

        splitContainer.SizeChanged += apply;
        splitContainer.Disposed += apply;
        apply(splitContainer, EventArgs.Empty);
    }
}
