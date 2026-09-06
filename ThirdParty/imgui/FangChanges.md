# Dear ImGui への変更点

版: 1.91.9b。上流を更新するときはここに書いた 2 か所を読み直し、同じ形で当て直す。
条件は [ADR-053](../../設計判断記録.md#adr-053)（このリポジトリでは notes 側に置いてある）。

## 1. `imconfig.h`

末尾に、代替グリフへ落ちたコードポイントを通知するフックを 1 つ追加した。

```cpp
namespace fang
{
	void NoteMissingGlyph(unsigned int codePoint);
}
#define IM_MISSING_GLYPH_HOOK(c) fang::NoteMissingGlyph((unsigned int)(c))
```

`fang::NoteMissingGlyph` の実体は `Core/Text/MissingGlyphCounter.cpp` にある。imgui は `Core` を参照しない
（exe のリンク時に解決される）。

## 2. `imgui_draw.cpp` の `ImFont::FindGlyph`

代替グリフを返す 2 か所に `IM_MISSING_GLYPH_HOOK(c)` を挟んだ。

```cpp
ImFontGlyph* ImFont::FindGlyph(ImWchar c)
{
    if (c >= (size_t)IndexLookup.Size)
        return IM_MISSING_GLYPH_HOOK(c), FallbackGlyph;
    const ImU16 i = IndexLookup.Data[c];
    if (i == (ImU16)-1)
        return IM_MISSING_GLYPH_HOOK(c), FallbackGlyph;
    return &Glyphs.Data[i];
}
```

`FindGlyphNoFallback` には入れていない（欠字の判定用に「無ければ null」を返す本来の役目のまま使う）。

## 更新時の当て方

1. 新しい版の `imgui_draw.cpp` の `ImFont::FindGlyph` を開き、上と同じ形で 2 行差し込む
2. 新しい版の `imconfig.h` の末尾に、上のブロックをそのまま追記する
3. 起動時のログに「欠字の検出フックが効いていない」が出ないことを確認する（`EditorUI::Initialize` の自己診断）
