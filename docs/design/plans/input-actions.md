# 入力のアクション化(図の8・9ページを更新)

要件: `docs/design/plans/input-actions-requirements.md`

## 結論

- ゲームのコードは**アクション**で書く(`actions.Get(Action::Roll).IsTapped()`)。どのボタンやキーがそのアクションかは`actions.json`に書く
- パッドのボタンは**位置の名前**(`Gamepad/South`など)で呼ぶ。画面の表示(Aか ×)は、最後に使ったパッドの機種で切り替える
- プレイヤーが変えた割り当ては、既定(`actions.json`)との**差分だけ**をユーザー設定に保存する(Unityのbinding overrideと同じ)

## 1. 何が変わるか

| | 今 | 変更後 |
|---|---|---|
| ゲームが読むもの | `ControlState`(仮想パッド) | `ActionSet`の`ActionState` |
| JSON | `keybindings.json`:パッドのボタン → キー | `actions.json`:アクション → パッドのボタン・キー |
| パッドのボタン名 | A / B / X / Y | South / East / West / North(表示は機種ごと) |
| 割り当ての変更 | JSONを書き換える | キー割り当て画面で変え、差分を保存 |
| PCのパッド | XInput | SDL3(XboxのパッドとDualSense) |

- CharacterCommandから先は変わらない。PlayerControllerがボタンの代わりにアクションを読むだけ

## 2. データ形式

`actions.json`(ゲームが持つ既定値):

```json
{
  "maps": {
    "Gameplay": {
      "Move":   { "type": "Vector2", "bindings": [ "Gamepad/LeftStick", { "composite": "Keyboard/WASD" } ] },
      "Look":   { "type": "Vector2", "bindings": [ "Gamepad/RightStick", { "path": "Mouse/Delta", "scale": 0.0025 }, { "composite": "Keyboard/Arrows", "mode": "rate" } ] },
      "Attack": { "type": "Button",  "bindings": [ "Gamepad/RightShoulder", "Mouse/Left" ] },
      "Heavy":  { "type": "Button",  "bindings": [ "Gamepad/RightTrigger", "Keyboard/Shift+Mouse/Left" ] },
      "Roll":   { "type": "Button",  "bindings": [ "Gamepad/East", "Keyboard/Space" ] },
      "LockOn": { "type": "Button",  "bindings": [ "Gamepad/RightStickPress", "Keyboard/Q" ] }
    },
    "UI": {
      "Navigate": { "type": "Vector2", "bindings": [ "Gamepad/LeftStick", "Gamepad/DPad", { "composite": "Keyboard/Arrows" } ] },
      "Submit":   { "type": "Button",  "bindings": [ "Gamepad/South", "Keyboard/Enter" ] },
      "Cancel":   { "type": "Button",  "bindings": [ "Gamepad/East", "Keyboard/Escape" ] }
    }
  }
}
```

- パッドのボタン名:`South / East / West / North`、`LeftShoulder / RightShoulder`、`LeftTrigger / RightTrigger`、`LeftStick / RightStick`、`LeftStickPress / RightStickPress`、`DPad`、`Start / Select`
- 修飾キー付き(`Shift+Mouse/Left`)は既存どおり優先する
- 矢印キーでの視点操作は`Look`に常に入れておく(既存のKeyboardOnlyプロファイルの代わり。プロファイルの切り替えはなくす)
- 割り当て画面で変えられるのは、`Gameplay`マップのButtonのアクションの「パッド用1つ」と「キーボード・マウス用1つ」。Vector2(移動・視点)とUIのマップは変えない

ユーザー設定(`input_overrides.json`、プレイヤーが変えたところだけ):

```json
{ "Gameplay/Roll": { "keyboard": "Keyboard/LeftAlt" } }
```

- 保存先はPCがユーザーのアプリデータ、Xboxがアプリのローカルフォルダ

## 3. クラス

```
InputManager  (エンジン。既存を作り替え)
+ Update()                                  // Main。1 フレームの最初(既存どおり)
+ GetActions(player = 0) : const ActionSet& // そのフレームは不変。ジョブからも読める(既存どおり)
+ SetActiveMap(player, "Gameplay" / "UI")
+ BeginRebind(player, action, DeviceKind) / IsRebinding() / CancelRebind()
+ SaveOverrides() / LoadOverrides()
+ GetBindingDisplay(player, action) : BindingDisplay   // 画面に出す名前・アイコン
+ GetLastUsedDevice(player) : Gamepad(Xbox / PlayStation) / KeyboardMouse
+ SetVibration(player, low, high, seconds)  // 既存どおり

ActionSet    : ActionState をアクションの数だけ持つ。ActionId(名前のハッシュ)で引く
ActionState  : IsDown / IsPressed / IsReleased / IsTapped / IsHeld / GetLastPressedTime / GetVector2
               (短押し・長押し・先行入力の判定は、既存の ControlState と同じものをアクションに付け替え)
InputBinding : path ("Gamepad/East" など) / composite / scale

GamepadReader (プラットフォーム別)
  Xbox : Windows.Gaming.Input(既存)
  PC   : SDL3。Xbox のパッドも DualSense も同じ GamepadState(位置の名前)に読む
```

- `Look`は既存の`GetLookDelta`と同じく、スティック(速さ × dt)とマウス(移動量)を合算する
- `ControlState`と`KeyBindings` / `PadBinding`はなくなる(役目はActionSetとactions.jsonに移る)
- 操作を受け付ける画面(メニュー・キー割り当て画面など。`UIScreen::capturesInput`がtrue)が出ている間だけ`UI`のマップに切り替える。HUDはfalseなので、プレイ中は`Gameplay`のまま。UIのボタン操作は`Submit` / `Cancel` / `Navigate`を読む(マウスのクリックは既存どおり)

## 4. キー割り当て画面

- ゲーム側の`KeyConfigScreen`(5ページ目のUIScreen)。アクションの一覧に、パッドとキーボードの割り当てを並べて表示する(表示は`GetBindingDisplay`)
- 項目を選ぶと`BeginRebind(player, action, DeviceKind)`。InputManagerは次に押されたそのデバイスのボタン・キーを新しい割り当てにする
  - 他のアクションと重なったら、そのアクションと入れ替える
  - Escape(パッドはStart)で取り消す
- 画面を閉じるときに`SaveOverrides()`。起動時に`LoadOverrides()`で既定の上に重ねる
- 「既定に戻す」は差分を消すだけ

## 5. 画面のボタン表示

- `GetBindingDisplay`は、割り当てのパスと「最後に使ったパッドの機種」から表示を決める
  - `Gamepad/South` → Xboxなら「A」、PlayStationなら「×」
  - `Keyboard/Space` → 「Space」
- 機種の判別はGamepadReaderが行う(SDL3はパッドの種類を返す。Xbox版は常にXbox)

## 6. 他のページとのつながり

| ページ | 変更 |
|---|---|
| 8入力 | InputManagerの作り替え。ControlState / KeyBindings / PadBindingをActionSet / ActionState / InputBindingに置き換え。SetProfileとKeyboardOnlyプロファイルは削除。GamepadReaderのPCをSDL3に。keybindings.jsonの例をactions.jsonに |
| 9操作 | PlayerControllerは`GetActions()`を読む(`IsTapped(B) → roll`を`Roll.IsTapped() → roll`に) |
| 5 UI | KeyConfigScreenを例として追加。UIScreenに`capturesInput`を追加。UIの操作はUIマップのアクションを読む |

## 7. 検討した案

| 案 | 却下理由 |
|---|---|
| 今の仮想パッドのまま、キーボードの割り当てだけ画面で変える | ゲームのコードにボタン名が残り、パッドの割り当てを変えられない |
| ボタン名をA / B / X / Yのまま使う | PSでは × がAになり、表示との対応が分かりにくい。新しいエンジン(Unreal・Unity・SDL3)は位置の名前 |
| 割り当てを丸ごとユーザー設定に保存する | ゲームの更新で既定を変えたとき、古い設定が新しいアクションを隠す。差分だけなら既定の変更が反映される |
| PCもWindows.Gaming.Inputで読む | DualSenseに対応していない |
