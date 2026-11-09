# NX PCK Studio

[PCK-studio-Web](https://github.com/Techn0cok1qq/PCK-studio-Web) と同じ画面構成(メニュー / アセット一覧 / プレビュー / アセット詳細)で、
スキンパックの `.pck` を作って次の場所に置く Switch 自作ソフトです。

    sd:/atmosphere/contents/01006BD001E06000/romfs/NX/DLC/<パック名フォルダー>/<ファイル名>.pck

PCKの書き出しは Web PCK Editor / PCK-studio-Web の処理を移植したもので、GPL-3.0 です。

- 作者: cokacolastic / バージョン: 1.0.0

## 使い方
1. `nxpckstudio.nro` を `sd:/switch/` に置いて起動(初回に `sd:/switch/nxpckstudio/` が作られ、サンプル画像が `images/` に入ります)。
2. 自分のスキン(64x64 / 64x32)のPNGを `sd:/switch/nxpckstudio/skins/` に、マント(64x32)のPNGを `capes/` に入れる。
3. 「スキン追加」→ 画像を選ぶ → スキン名を入力。左の一覧にアセット(スキン・マント・localisation.loc)が並びます。
4. 一覧でアセットを選び、右下の「アセット詳細」で名前・テーマ名・スキンID・モデル・マント・プロパティ(キー/値の編集・追加・削除)を変更。
5. 「保存して配置」→ ① パックのフォルダー名 ② PCKファイル名 を入力すると、上の場所に書き出します。
6. ゲームを再起動。

## 操作 (ボタン / タッチどちらも可)
| 場所 | 操作 |
|---|---|
| 一覧 | ↑↓ 選択 / A か → で詳細へ / X スキン追加 |
| 詳細 | ↑↓ 選択 / A 編集 / X キー編集・マント解除 / Y プロパティ削除 / B か ← で一覧へ |
| メニュー | L で出入り / ←→ 選択 / A 実行 |
| 共通 | + 終了(未保存なら確認) |

画像は 32767 バイト以下を推奨(超えるとゲームが落ちる恐れがあるため、一覧が赤くなり、保存時に確認します)。
`sd:/switch/nxpckstudio/config.ini` では `endian=little|big` だけ変えられます(出力先は固定)。

## フォント
HTML の `font-family: MCFont → Pixelify Sans → DotGothic16` と同じ順で、1文字ずつ使えるフォントを探して描きます
(どれにも無い文字は本体の共有フォント)。MCFont は `romfs/minecraft.ttf`。Pixelify Sans / DotGothic16 は
GitHub Actions のビルド時に Google Fonts(OFL)から自動で `romfs/` に入ります。手元でビルドする場合は
`romfs/PixelifySans.ttf` と `romfs/DotGothic16.ttf` を自分で置いてください。

## ビルド
GitHub に push すると Actions が `nxpckstudio.nro` を作ります(devkitPro: switch-sdl2 / sdl2_image / sdl2_ttf)。

## extras/
Discord のチャンネルから画像を取る同期処理(未使用・ビルド対象外)。

## PC上のテスト (devkitPro 不要)
    cd tests && g++ -std=c++17 -o test_pck test_pck.cpp && ./test_pck skin.png cape.png out.pck
    # verify.js は PCK-studio-Web の index.html から PckParser/LOCParser を抜き出して使います
    sed -n '/class PckParser/,/UI コントローラ/p' /path/to/PCK-studio-Web/index.html > parsers.js
    node verify.js out.pck

## BGM
内蔵の曲(オリジナル)が流れます。`sd:/switch/nxpckstudio/bgm/` に mp3 / wav / ogg を入れると、その曲が順番に流れます。
メニューの「BGM」でオン/オフ、`config.ini` の `bgm=off` で起動時から切れます。

## 操作の追加
十字キーと左スティックは長押しで連続移動、右スティックは3Dプレビューの回転、`−` は「保存して配置」です。
