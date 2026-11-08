# コーディング規約

このリポジトリのC++のコードの書き方を決める。決まったものから足していく。
字下げ・改行コード・文字コードは`.editorconfig`が保存のたびに揃えるので、ここには理由だけ書く。
行末の空白は保存では消えないので、保存の前にCtrl+K, Ctrl+D(ドキュメントのフォーマット)で整形する。

## ファイル

- C++のソースはBOM付きUTF-8・CRLF。MSVCが日本語のコメントを`/utf-8`なしで正しく読むため。
- HLSLのソースはBOMなしUTF-8・CRLF。`fxc`はBOMを読めず、`X3000`で止まるため。
- 字下げはタブ。行末コメントの位置合わせはスペース。
- 連続する宣言・代入の`=`は縦にそろえる。そろえる空白はスペースで入れる(タブは幅の設定で崩れる)。
  整形はそろえないので手で入れる(`.editorconfig`の`cpp_space_around_assignment_operator = ignore`で、整形が消さないようにしてある)。
- メンバの宣言は名前の位置も縦にそろえる。`*`は型に付ける(`void*        m_block`)。整形が`*`の周りの空白を消さないように、`.editorconfig`の`cpp_space_pointer_reference_alignment`を`ignore`にしてある。
- HLSLの構造体の名札(`: POSITION`)は縦にそろえない。VSの整形が左に詰めるため。

## 命名

- 識別子は略さない(`previous`、`Initialize`、`position`)。略しても通じるかを毎回考えずに済むようにするため。`ptr`や`prev`のような広く通じる略語も例外にしない。
  例外として、`_DESC`の型のローカル変数は型名に合わせて`Desc`で終える(`D3D12_QUERY_HEAP_DESC`➡`queryHeapDesc`)。型名に同じ略があり、読み方に迷わないため。
  拡張子そのものは略とみなさない(`exePath`、`csoFileName`)。ファイルに同じ綴りがあり、読み方に迷わないため。
  例外はウィンドウプロシージャの`wParam`と`lParam`だけ。中身がメッセージごとに変わり、合う名前を1つに決められないため。意味のある名前は、メッセージを受けて中身を取り出すところで付ける(`WM_SIZE`なら`width`)。
- 同じ役割の型はProjectBeastの名前を借りる。
- シングルトンの取得は`GetInstance()`。
- 定数は大文字のスネークケース(`HEADER_SIZE`)。
- ローカル変数は小文字で始める(`device`、`swapChainDesc`)。大文字で始まる型や関数と見分けるため。
- メンバ変数は`m_`から始める(`m_size`)。
- 名前は中身と合わせる。返り値の型を名前に書かない(`MallocMemoryPtr`ではなく`MallocMemory`)。
- ポインタの変数名には、何を指すかを書く。ポインタであることは型でわかる(`ptr`ではなく、mallocが返した先頭なら`block`、呼ぶ側に返す番地なら`data`)。
- 同じものを指す変数は、関数が違っても同じ名前にする。
- 返り値を受ける変数は、関数の中に1つだけなら`result`と書く。何の結果かは呼ぶ行で読めるため。
  2つ以上あるときは、呼んだ関数の名前に`Result`を付ける。APIの印と版の番号は落とす(`D3D12CreateDevice`➡`createDeviceResult`、`CreateDXGIFactory2`➡`createFactoryResult`)。
  同じ名前の関数を別のオブジェクトで呼ぶときは、何のかを足す(`commandAllocator->Reset()`➡`resetCommandAllocatorResult`)。

## 型

- メンバを外から直接読み書きしてよいデータの入れ物は`struct`、`private`なメンバを関数で守るものは`class`。
- 大きさと個数は`size_t`。
- 構造体の変数は`{}`を付けて宣言する(`DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};`)。欄を書き忘れたときに、でたらめな値が残らないようにするため。
- 値で返すゲッターの返り値に`const`を付けない。意味を持つのは参照やポインタで返すときだけ。
- ポインタの判定は`nullptr`と比べて書く(`if (m_head != nullptr)`)。`if (m_head)`でも動くが、比べて書けば整数や`bool`の判定と見分けがつく。
- 右辺から型が読めないときは型を書く(`HRESULT presentResult = swapChain->Present(1, 0);`)。読めるときは`auto`でよい(`auto currentFrameTime = std::chrono::steady_clock::now();`)。
  `auto`の型はコンパイルのときに決まるため、実行の速さは変わらない。

## 置き場所

- .cppの中だけで使う定数と型は、その.cppの無名名前空間に置く。
- クラスの中だけで使う型は、.hのクラスの`private:`に前方宣言だけを書き、中身は.cppで`struct Memory::Header { ... };`の形で定義する。
  中身を書き換えても、その.hをincludeするファイルがコンパイルし直しにならない。
- 使うものは、そのファイルで直接includeする。ほかのヘッダーがたまたま読み込んでいるものに頼らない。
  対になる.hがincludeしているものは、.cppで書き直さない。
- `FangEngine`のコードは`fangEngine`の名前空間で包み、その中で直下のフォルダごとに、フォルダ名を小文字にした名前空間で包む(`fangEngine::core::Memory`、`fangEngine::render::RenderContext`)。
  どの層のクラスかをコードから読めるようにするため。`Core/Memory`のように深いフォルダでも、名前空間は増やさない。
  直下のファイルは`fangEngine`だけで包む(`fangEngine::Application`)。
- 2段の名前空間は`namespace fangEngine::render`と1行で書く。字下げが2段にならないようにするため。

## 開発用のコード

- 判定は`FANG_DEBUG_OR_PROFILE`で行う(`FangEngine.vcxproj`がDebugとProfileの構成で定義する)。`_DEBUG`はProfileで定義されないため使わない。
- 判定は1か所に集める。値が違うだけなら、名前を1つにして値だけを`#ifdef`で書き分け、使う行には`#ifdef`を書かない。
  使う行ごとに書き分けると、囲み漏れが起きる。
- `#endif`には、短いブロックでも`// FANG_DEBUG_OR_PROFILE`を付ける。ブロックは後から伸び、伸ばすときに付け足すのを忘れるため。
- 構成で切り替えるための中身が空の関数は、空行を空けずに並べてよい。

## エラーの出し方

- エラーの文は`core::LogError`で出す。`std::cerr`は使わない。出し先を1か所で替えられるようにするため。
- `LogError`はVSの出力と`MessageBoxW`の両方に出す。今のエラーは`GpuProfiler`の`Map`の1か所を除いてアプリの終了につながり、出力だけだとほかの文に埋もれ、VSの外では見えないため。
  UWPでは`MessageBoxW`を使えない➡UWPの構成を作るときに差し替える。
- `HRESULT`の確認は`core::LogIfFailed`で行い、呼ぶ行と確認の行を分ける(`if (core::LogIfFailed(result, L"フェンスの作成に失敗しました。")) return false;`)。引数の多い関数でも、文が閉じ括弧の後ろに離れないようにするため。
- 確認をマクロにしない。関数でも1行で書け、マクロにしても短くならず、`return`が見えなくなるだけのため。
- コンソールの窓を出さない。`SubSystem`は3つの構成とも`Windows`にし、`EntryPointSymbol`に`mainCRTStartup`を指定して`int main()`を残す。ゲームの窓の上に黒い窓が重なるのを避けるため。

## クラスの形

- 途中の`public:` / `protected:` / `private:`の上は2行空ける。

## ifの書き方

- 早期リターンと、ループを抜ける`break`だけは1行で書く(`if (block == nullptr) return nullptr;`、`if (running == false) break;`)。
- それ以外は中身を別の行に書き、`{`と`}`で囲む。処理を1行に並べると横に長くなって読みにくいため。
- 中身が5行までなら`{`を条件式と同じ行に置く。6行以上なら`{`も別の行に置く。

## コメント

- 関数の説明はDoxygen形式で.hに書く(`@brief`、`@param`、`@return`)。
  無名名前空間に置いた、そのファイルの中だけで使う関数は.hがないため、.cppの定義の上に書く。
- メンバ変数には行末コメントを付ける。
- 文が2つ以上のコメントは「。」で改行する。
- 名詞で終わる行末コメント(`// 合計メモリサイズ`)には「。」を付けない。
- 日本語の文の中では、数字や英単語の前後に空白を入れない(`// size_t型の最大値`、`1/30秒`)。Markdownの文書とコミットメッセージも同じ。

## コミットメッセージ

- 件名は「領域: 〜した」の形で書く(`Engine: D3D12のDeviceとCommandQueueを作った`)。領域は`Core`、`Engine`、`Docs`のどれか。
- 本文も過去形で書き、そのコミットで変えたことだけを書く。
- PRのタイトルも件名と同じ形にする。

## 決めていないこと

- ゲームを止めない知らせの出し先(ImGuiのコンソール)。素材が見つからないときの知らせなどを書くときに決める。
- 1行のゲッターを強制インラインにするか(`__forceinline`とDebugの`/Ob1`)。今は`inline`。フレームループができてから決める。
