# うまピタ

## これは何
ウマ娘のゲームウィンドウを画面にフィットさせるやつ。
使い方は何となく察してください。

## インストール方法
https://github.com/AoiMoe/umapita/releases から umapita-YYYYMMDD-NN.zip みたいな名前の zip ファイルを落とすと、
中に umapita.exe と umapita_keyhook.dll というファイルが入っているので、これを適当なところに置いて起動してください。
インストーラはありません。

## 注意事項
- 最近の Windows では他のプロセスのウィンドウを操作するのに何かしらの権限が必要なので、
  実行すると UAC の権限昇格ダイアログが出ます。
  （出ないようにするには署名したりしないといけないので、それなりにお金もかかるし面倒くさい）
- 領域を「外枠」にしたときに左右と下に少し隙間があるように見えますが、これは仕様です。
  見えない枠がそこに存在しています（マウスカーソルを持っていってみるとわかります）。
- 最小化ボタンを押すとタスクバーから消えますが、タスクバーの通知領域に「UMPT」という感じのアイコンがあるはずなので、
  それをクリックしてみてください。
- ゲームウィンドウ上でALT+数字（プロファイル切り替えのショートカットキー）が押されたことを検出するために SetWindowsHookEx を使っています。
  DLL インジェクションに技術的な抵抗感があるなら umapita_keyhook.dll を削除すれば無効になります（無くてもそれ以外の機能は動くようになっています）。

## ビルド方法
ビルド環境は msys2 専用。コンパイラは mingw32 の新しめの gcc なら大丈夫。
ウマ娘の Unity Player が 32bit なので、SetWindowsHookEx の制約により、こちらも 32bit アプリケーションとしてコンパイルする必要があります。

## TODO
- ダイアログの数値入力を改善する
- ドキュメント
- 使い方デモ
