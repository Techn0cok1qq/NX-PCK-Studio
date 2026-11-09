# NX PCK Studio

スイッチ内のスキン展開図・マント画像(PNG)から、スキンパックの `.pck` を作って
`sd:/atmosphere/contents/01006BD001E06800/romfs/NX/<folder>/` に置く Switch 自作ソフトです。
PCKの書き出しは [PCK-studio-Web](https://github.com/HINATIR/Web-PCK-Editor) の処理を移植したもので、GPL-3.0 です。

- 作者: cokacolastic / バージョン: 1.0.0

## 使い方
1. `nxpckstudio.nro` を `sd:/switch/` に置いて起動(初回に `sd:/switch/nxpckstudio/` が作られ、サンプル画像が `images/` に入ります)。
2. 自分のスキン(64x64 / 64x32)・マント(64x32)のPNGを `sd:/switch/nxpckstudio/images/` に入れる。
3. 「スキンを作る」→ 名前・モデルを設定 → スキン展開図 / マント画像を選ぶ → 「作成して配置」。
4. ゲームを再起動。

`sd:/switch/nxpckstudio/config.ini` で出力フォルダー名(`folder`)、タイトルID、エンディアン(`endian=little|big`)を変えられます。
画像は 32767 バイト以下(超えるとゲームが落ちる恐れがあるため、作成を止めます)。

## ビルド
GitHub に push すると Actions が `nxpckstudio.nro` を作ります(devkitPro: switch-sdl2 / sdl2_image / sdl2_ttf)。

## extras/
Discord のチャンネルから画像を取る同期処理(未使用・ビルド対象外)。

## PC上のテスト (devkitPro 不要)
    cd tests && g++ -std=c++17 -o test_pck test_pck.cpp && ./test_pck skin.png cape.png out.pck
    # verify.js は PCK-studio-Web の index.html から PckParser/LOCParser を抜き出して使います
    sed -n '/class PckParser/,/UI コントローラ/p' /path/to/PCK-studio-Web/index.html > parsers.js
    node verify.js out.pck
