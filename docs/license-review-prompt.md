# 他AIレビュー依頼：本リポジトリのライセンス境界（BTstack第4条関係）

以下をそのまま別AIに貼って意見を求めるためのプロンプト。結論の要約ではなく判断材料ごと渡すこと。

---

あなたはソフトウェアライセンス（特に組込みOSSの結合・頒布条件）に詳しいレビュアーです。
Raspberry Pi Pico 2 W用ファームウェアのリポジトリ（自作コード＋ビルド時リンクする第三者コードあり）
について、以下の事実関係のもとでライセンス境界を評価してください。推測と確定の分離を明示し、
確信度（高／中／低）を各判断に付けてください。

## 確定している事実

1. ファームウェアは Raspberry Pi Pico SDK 2.3.0 に同梱の BTstack（BlueKitchen GmbH）をリンクする。
   同梱ライセンス文（SDK内 `lib/btstack/LICENSE` の写しをリポジトリの `LICENSES/BTstack.txt` に保管）は
   BSD系3条項＋第4条「Any redistribution, use, or modification is done solely for
   personal benefit and not for any commercial purpose or for monetary gain.」を含む。
   商用は `contact@bluekitchen-gmbh.com` への問合せと明記されている。
2. リポジトリ自作コードは MIT（`LICENSE`）とした。SDK自体は同梱・再頒布していない
   （ビルド依存のみ）。リポジトリ直下に `LICENSE` 以外のライセンスファイルは存在しない。
3. 成果物は UF2バイナリ（BTstackリンク済み）と、BTstackを含まないホスト単体試験コード
   （`tests/host/`、純粋C、BTstack非依存）である。
4. 用途は個人の hobby（非商用）であり、現時点で販売・有償頒布の予定はない。

## 質問（各々に確信度付きで回答）

1. UF2バイナリの無償公開（GitHub release等・非商用）は第4条上許容されるか。
2. プリフラッシュ済みPicoの販売（ハード代＋手数料）は「commercial purpose / monetary gain」に当たるか。
3. 受託開発（他人のための有償改変・書込代行）はどうか。開発行為自体と成果物引渡しを分けて評価してほしい。
4. 本リポジトリ自作部分のMIT表示は、BTstackリンク済みバイナリについて誤解を招かないか。
   より適切な表示（例：全体の実効条件の明示方法）があれば提案してほしい。
5. `tests/host/` のようにBTstack非依存の部分だけを商用利用する場合、切分けは有効か。
6. 上記以外に見落としているリスク（特許・商標：NintendoのVID/PID・名称の使用、等）があれば指摘してほしい。
   ※注：ファームは `057E:2009`・"Pro Controller" 等の任天堂識別子をエミュレーション目的で使用している。

## 回答形式

- 各質問への回答＋確信度＋根拠条文
- 推奨する対応（ファイル構成・表示文面の具体案があれば）
- 追加で確認すべき事実のリスト
