# Waylaunch

[English](README.md) | [日本語](README.ja.md)

Waylaunchは、Waylandデスクトップ向けの軽量なアプリケーションランチャーです。入力中の文字でインストール済みアプリを検索し、選択したアプリを起動します。

## 主な機能

- `~/.local/share/applications`、`/usr/share/applications`、`/usr/local/share/applications` にある `.desktop` ファイルを検索します。
- キーボード操作とマウス選択に対応しています。
- コンポジターがWayland text-input-v3プロトコルをサポートしている場合、IME入力を利用できます。
- `>` に続けて入力したコマンドを実行できます。
- `=` に続けて入力した四則演算式を計算し、Enterキーで結果をWaylandクリップボードにコピーできます。小数、括弧、単項の `+` / `-` に対応しています。
- 通常起動時はデバッグログを抑制し、`--debug` または `-d` を指定したときに表示します。

## 必要環境

- `wl_compositor`、`wl_shm`、`xdg-shell` プロトコルに対応したWaylandセッション
- ビルド時はGCC、Make、`dpkg-dev`、Wayland、xkbcommon、FreeType、libpngの開発用パッケージ

DebianまたはUbuntuでは、次のコマンドでビルド依存パッケージをインストールできます。

```sh
sudo apt install build-essential dpkg-dev libwayland-dev libxkbcommon-dev libfreetype-dev libpng-dev
```

## ビルドと起動

```sh
make
./waylaunch
```

デバッグログを有効にして起動する場合:

```sh
./waylaunch --debug
```

`make run` でもデバッグログ付きで起動できます。

Debianパッケージを作成するには、次を実行します。

```sh
make deb
```

生成したパッケージをインストールします。ファイル名の末尾はシステムのアーキテクチャによって異なります。

```sh
sudo apt install ./waylaunch_1.1.0_$(dpkg --print-architecture).deb
```

## 操作方法

| キー・操作 | 動作 |
| --- | --- |
| 文字入力 | アプリ名を絞り込みます |
| 上下矢印キー | アプリを選択します |
| Enterキーまたは左クリック | 選択中のアプリを起動します |
| `>` に続けてコマンドを入力し、Enter | コマンドを実行します |
| `=` に続けて計算式を入力 | 計算結果をプレビューします。Enterで結果をクリップボードにコピーします |
| Escキー | Waylaunchを終了します |

計算式では `+`、`-`、`*`、`/`、括弧を使用できます。無効な式の場合は結果欄に `Invalid expression` と表示されます。

## リリース

`v1.1.0` のようなバージョンタグをpushすると、GitHub Actionsがタグに対応したバージョンのDebianパッケージを作成し、GitHub Releaseを公開します。

```sh
git tag v1.1.0
git push origin v1.1.0
```
