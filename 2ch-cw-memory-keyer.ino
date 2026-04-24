// =====================================================================
//   Project:  2CH Electronic Memory Keyer Type2 [EK_T2]
//   Version:  1.0.0 (Public Release)
//   Author:   N.Nagae (JI2OJV) / NORI-Works
//   License:  GNU General Public License v3.0
//   Copyright (c) 2026 N.Nagae (JI2OJV)
// ---------------------------------------------------------------------
// [Description / 概要]
//   ATtiny85を使用した、省電力・2chメモリ搭載のCWキーヤー。
//   - パドルによるメッセージ記録（EEPROM保存）
//   - ボタン操作によるKey速度調整（10-30WPM）
//   - 送信(Tx) / 練習(Tone) の物理スイッチ切り替え（設定時は送信ガード付）
// =====================================================================

/* --- License Notice ---
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

/* --- Release History / 公開履歴 ---
 * 2026.04.23 | Ver 1.0.0 | 初回公開版 (GitHub Public Release)
 * ---------------------------------------------------------------------
 * [Internal Development Archive]
 * 2026.02.16 - 2026.03.03 : Development from Ver 0.0 to Ver 2.11
 * (Optimized for ATtiny85, EEPROM logic, and Field Operation)
 */


#include <EEPROM.h>

// --- コンパイルスイッチによる環境切り替え ---
#if defined(__AVR_ATtiny85__)
  // ATtiny85用の設定 (PBxの番号で指定)
  const int PIN_DOT     = 0;    // 物理5pin (PB0)
  const int PIN_DASH    = 1;    // 物理6pin (PB1)
  const int PIN_TX_TONE = 2;    // 物理7pin (PB2)
  const int PIN_LED     = 4;    // 物理3pin (PB4)
  const int PIN_SW      = 3;    // 物理2pin (PB3 / ADC3)
#else
  // Arduino Nano (ATmega328P)設定
  const int PIN_DOT     = 2;    // D2 pin
  const int PIN_DASH    = 3;    // D3 pin
  const int PIN_TX_TONE = 4;    // D4 pin
  const int PIN_LED     = 5;    // D5 pin
  const int PIN_SW      = A7;   // A7 (for A/D)
#endif
// ---------------------------------------


// ==========================================
// 1. ラベル定数
// ==========================================
const int MODE_TX       = 100;  //切替スイッチTX側（数値に意味なし）<-- 関数の戻り値(int)とするため
const int MODE_TONE     = 200;  //切替スイッチTone側（数値に意味なし）
const byte DEF_WPM       = 20;  //WPM初期値[wpm]
const byte WPM_MAX       = 30;  //Key Speed Max
const byte WPM_MIN       = 10;  //Key Speed Min
const byte NUM_MEMO_CH   = 2;   //メモリCH数
const int DEF_TONE_FREQ = 400;  //Tone周波数デフォルト値[Hz]
const unsigned long UI_DURATION = 75;       //UI用のLED点灯・Tone音吹鳴時のON時間[ms]
const unsigned long UI_DURATION_ERR = 300;  //Mode処理失敗時のLED点灯・Tone音吹鳴時のON時間[ms]


// ==========================================
// 2. グローバル変数
// ==========================================
bool g_isTxMode = false;                    // true:Tx / false:Tone
byte g_currentWpm = DEF_WPM;                // 現在のKeySpeed
unsigned long g_durationDot = 1200/DEF_WPM; // Dot時間
int g_currentToneFreq = DEF_TONE_FREQ;      // 現在のTone周波数
bool g_flgDot = false;                      // Dot処理中フラグ
bool g_flgDash = false;                     // Dash処理中フラグ
bool g_isMainLoop = false;                  // Setup/Loop処理切り分けフラグ

// ==========================================
// 3. EEPROM関連
// ==========================================
const int MEMO_CODE_MAX = 80;   //各チャネル毎の記憶符号数(スペース含む)Max...EEPROM容量と関係

typedef struct{
    byte numOfCode;             //各チャネル毎の記憶符号数
    byte code[MEMO_CODE_MAX];   //符号記憶領域確保
} MemoCode;

// EEPROMアドレス配置
const byte EEPROM_MAGIC_NUM = 0x55;     // 識別番号（何でも良いですが0xFF以外）
const int ADDR_MAGIC        = 511;      //511番地を判定用に使用
const int ADDR_MEMO_0       = 0;        // 1番地からCH1
const int ADDR_MEMO_1       = 0 + sizeof(MemoCode); // CH1の後ろからCH2
const int ADDR_MEMO_WPM     = 500;      // WPMは最後尾固定

MemoCode g_currentMemo[NUM_MEMO_CH];    //現在の各チャネル記憶内容
MemoCode g_workingMemo;                 //記録用符号作成用

// ==========================================
// 4. 初期処理
// ==========================================
void setup() {
// --- コンパイルスイッチによる環境切り替え ---
#if defined(__AVR_ATtiny85__)
    pinMode(PIN_DOT, INPUT);            // DOTパドル
    pinMode(PIN_DASH, INPUT);           // DASHパドル
#else
    pinMode(PIN_DOT, INPUT_PULLUP);     // DOTパドル（内蔵プルアップ）
    pinMode(PIN_DASH, INPUT_PULLUP);    // DASHパドル（内蔵プルアップ）
#endif    
    pinMode(PIN_TX_TONE, OUTPUT);       // 送信 or トーン出力（送信信号）
    pinMode(PIN_LED, OUTPUT);           // モニターLED出力
  // PIN_SWはanalogReadで使うため、pinModeの設定は不要

    delay(500); //IO設定待ち
    
    g_isMainLoop = false;   // Setup中

/* EEPROM 初期化判定 */
    if (EEPROM.read(ADDR_MAGIC) != EEPROM_MAGIC_NUM) {
        // === 初回起動時の処理 ===
        memset(g_currentMemo, 0, sizeof(g_currentMemo));    //g_currentMemoの初期化
        
        EEPROM.put(ADDR_MEMO_0, g_currentMemo[0]);
        EEPROM.put(ADDR_MEMO_1, g_currentMemo[1]);
        
        g_currentWpm = DEF_WPM;
        EEPROM.update(ADDR_MEMO_WPM, g_currentWpm);
        
        // 最後に「初期化完了」の印を書き込む
        EEPROM.update(ADDR_MAGIC, EEPROM_MAGIC_NUM);
    }
    else {
        // === 2回目以降の通常読み出し ===
        EEPROM.get(ADDR_MEMO_0, g_currentMemo[0]);
        EEPROM.get(ADDR_MEMO_1, g_currentMemo[1]);
        g_currentWpm = EEPROM.read(ADDR_MEMO_WPM);
    }

/* wpmガード処理 */
    if( g_currentWpm < WPM_MIN || WPM_MAX < g_currentWpm )
        g_currentWpm = DEF_WPM;
  
/* 短点時間算出 */
    g_durationDot = CalcDotTime( g_currentWpm );  

// --- 起動時のスイッチ状態を確定 --- //
    GetButtonState();

/* 現在のKeySpeedアナウンス */
    AnnounceWpm( g_currentWpm );
    
/* Main loopへ移行 */
    g_isMainLoop = true;
}

// ==========================================
// 5. メイン処理
// ==========================================
void loop() {
    int targetCh = 0;   // 記憶CH...0:ch1, 1:ch2

  // 現在のボタン状態
    int currentBtn = GetButtonState();
    
  // Tx/Tone状態確定
    if (currentBtn == MODE_TX ||
        ( 1 <= currentBtn && currentBtn <= 3 )) g_isTxMode = true;
    if (currentBtn == MODE_TONE ||
        ( 4 <= currentBtn && currentBtn <= 6 )) g_isTxMode = false;
    
  // 各モード毎の処理
    int requestedMode = GetSelectedMode(); //モード番号確定

    switch ( requestedMode ) {
    // --- 0: 通常時（長押しされなかった場合）のみ再生を受け付け ---
        case 0:
            if( currentBtn == 2 || currentBtn == 5 )      targetCh = 1;
            else if( currentBtn == 3 || currentBtn == 6 ) targetCh = 2;
            else                                          targetCh = 0;
            
            // targetChが1または2のときだけ、かつ中身があるときだけ再生
            if (targetCh > 0) {
                // targetCh - 1 で配列の 0番目 or 1番目を指定
                if (g_currentMemo[targetCh - 1].numOfCode > 0) {
                    PlayFromMemory( targetCh );
                }
            }

            break;
        
    // --- 1: Key Speed変更 ---　※リセット処理も検討
        case 1:
            // 何かボタンかパドルの操作があれば false が返り、このifを抜けて下の判定へ進む
            if (CheckAndHandleTimeOut(5000)) break; // 5秒でタイムアウト
 
            ChangeKeySpeed( false ); // メモリへの記録なし、一時的な変更
            
            break;

    // --- 2: メモリ記録 ---(EEPROMへ書き込み）
        case 2:          
            //CH選択
            targetCh = 0;
            
// チャンネル選択待ちループ
            while (targetCh == 0) {
                // 何かボタンかパドルの操作があれば false が返り、このifを抜けて下の判定へ進む
                if (CheckAndHandleTimeOut(5000)) break; // 5秒でタイムアウト
                    
                int btn = GetButtonState();
                
                // ① やめたい時：Modeボタン(1 or 4)押し
                if (btn == 1 || btn == 4) {
                    IndicateMode(UI_DURATION_ERR, 1); // 長めのブザー音(1回)
                    break;
                }
                
                // 記録チャンネルの確定
                if (btn == 2 || btn == 5)      targetCh = 1;
                else if (btn == 3 || btn == 6) targetCh = 2;

                delay(10);
            }
            
            // チャンネルが選択された場合のみ記録へ（キャンセル時はスルー）
            if (targetCh > 0) {
                IndicateMode(UI_DURATION, targetCh); // CH1なら1回、CH2なら2回
                
                bool isRecorded = RecordToMemory(targetCh);
            
                if (isRecorded) {
                    // ② 記憶完了：CH1押しなら1回、CH2押しなら2回ブザー
                    IndicateMode(UI_DURATION, targetCh);
                } else {
                    IndicateMode(UI_DURATION_ERR, 1);
                }
            }
            
            break;
            
    // --- 3: KeySpeedをメモリへ記憶
        case 3:
            // 何かボタンかパドルの操作があれば false が返り、このifを抜けて下の判定へ進む
            if (CheckAndHandleTimeOut(5000)) break;

            ChangeKeySpeed( true );
            break;
    }

//Electronical Keyer処理(Dot,Dash判定)
    if( !digitalRead( PIN_DOT ) )   ProcessDot(false);
    if( !digitalRead( PIN_DASH ) )  ProcessDash(false);
}


// ==========================================
// 6. 関数定義
// ==========================================
// ==========================================
// 6-1. システム基盤・計算
// ==========================================
/* システム全体で使用する計算や、初期化に関する共通処理 */
/**
 * @brief Dot時間算出
         （PARIS標準: 1分間(60000ms)の中の50ユニット分 = 60000/wpm/50）
 * @return dot時間(unsigned long)
 */
unsigned long CalcDotTime( byte wpm ){
    if (wpm < WPM_MIN) wpm = WPM_MIN;
    if (wpm > WPM_MAX) wpm = WPM_MAX;
    
    unsigned long duration = 1200/wpm;
    return duration;
}


// ==========================================
// 6-2. ハードウェア出力層 (Keying)
// ==========================================
/* 送信機・トーン・LEDの物理的なON/OFF制御。すべての音と光の出口 */
/**
 * @brief Key On処理
 * @details
 * - Setup中とMain loop中を切り分けSetup中はTx出力はさせない
 * - Mode Tx : LED
 * - Mode Tone : LED + Tone On
 * @return なし
 */
void KeyOn( void ){
    digitalWrite( PIN_LED, HIGH );
    
    if( g_isMainLoop ){
        if( g_isTxMode ) digitalWrite( PIN_TX_TONE, HIGH );
        else tone( PIN_TX_TONE, g_currentToneFreq );
    }
    else{
        if( !g_isTxMode ) tone( PIN_TX_TONE, g_currentToneFreq );
    }
}


/**
 * @brief Key Off処理
 * @details 
 * - Setup中とMain loop中を切り分けSetup中はTx出力はさせない
 * - Mode Tx : LED
 * - Mode Tone : LED + Tone On
 * @return なし
 */
void KeyOff( void ){
    digitalWrite( PIN_LED, LOW );

    if( g_isMainLoop ){
        if( g_isTxMode ) digitalWrite( PIN_TX_TONE, LOW );
        else noTone( PIN_TX_TONE );
    }
    else{
        if( !g_isTxMode ) noTone( PIN_TX_TONE );
    }
}


// ==========================================
// 6-3. UI処理 (操作入力と状態通知)
// ==========================================
/* ユーザーの操作（入力）を解釈し、システムの状態（出力）を通知する対話層 */
// --- 入力系 ---
/**
 * @brief 各ボタンの状態取得
 * @details 
 * - 抵抗分圧比でTx/Toneを区別かつ押されているボタンを認識
 * - Vcc-AD         : 10k
 * - AD-ModeSW-Gnd  : 1k
 * - AD-CH1SW-Gnd   : 3k
 * - AD-CH2SW-Gnd   : 1k
 * - AD-ToneSW-Gnd  : 4.7k
 * @return 0:なし, 1/4:Mode, 2/5:CH1, 3/6:CH2, MODE_TX:Tx定常, MODE_TONE:Tone定常
 */
int GetButtonState() {
    int val = analogRead(PIN_SW);
    int rawBtn = 0;

    // --- 1. スイッチ位置の確定（定常状態で判定） ---
    // Tx Open(1023) と Tone Open(327) の中間付近で判定
    if (val > 600) {
        g_isTxMode = true;
    } 
    else if (val >= 290 && val <= 400) { 
        // Tone Open(327) 前後。CH2(248)と混同しないよう下限を290に設定
        g_isTxMode = false;
    }

    // --- 2. 各モードごとのボタン判定（閾値は実測値の中間） ---
    if (g_isTxMode) {
        // <Txモード実測: Mode:93 / Ch1:236 / Ch2:512 / Open:1023>
        if      (val < 160)  rawBtn = 1;         // Mode (93と236の間)
        else if (val < 370)  rawBtn = 2;         // CH1  (236と512の間)
        else if (val < 700)  rawBtn = 3;         // CH2  (512と1023の間)
        else                 rawBtn = MODE_TX;   // Open
    } 
    else {
        // <Toneモード実測: Mode:78 / Ch1:158 / Ch2:248 / Open:327>
        if      (val < 110)  rawBtn = 4;         // Mode (78と158の間)
        else if (val < 200)  rawBtn = 5;         // CH1  (158と248の間)
        else if (val < 290)  rawBtn = 6;         // CH2  (248と327の間)
        else                 rawBtn = MODE_TONE; // Open
    }

    // --- 3. チャタリング対策 ---
    static int lastStableBtn = 0;
    static int lastRawBtn = 0;
    static unsigned long lastDebounceTime = 0;
    const unsigned long debounceDelay = 50;

    if (rawBtn != lastRawBtn) {
        lastDebounceTime = millis();
    }
    lastRawBtn = rawBtn;

    if ((millis() - lastDebounceTime) > debounceDelay) {
        lastStableBtn = rawBtn;
    }   
  
    return lastStableBtn;
}


/**
 * @brief Mode取得
 * @details 
 * - Modeボタンの押している長さでMode1-3を切り分け
 * - 押している間は関数内でループし、離した瞬間に確定
 * @return Mode番号(1-3), 1秒未満で離した場合は0
 */
int GetSelectedMode() {
    int currentBtn = GetButtonState();
    static int lastBtn = 0;
    int selectedMode = 0;

  // 1. Modeボタン(1 または 4)が押された「瞬間」を検知
    if ((currentBtn == 1 || currentBtn == 4) && (lastBtn != 1 && lastBtn != 4)) {
        unsigned long btnPressStartTime = millis();
        int currentStep = 0;

    // 2. ボタンが押し続けられている間、このループから抜けない
    // currentBtnをループ内で更新し続けることで、指を離したことを検知可能にする
        while (currentBtn == 1 || currentBtn == 4) {
            unsigned long pressDuration = millis() - btnPressStartTime;

      // 3秒経過 (Step 3)
            if (pressDuration >= 3000 && currentStep < 3) {
                currentStep = 3;
                IndicateMode(UI_DURATION, currentStep);
            } 
      // 2秒経過 (Step 2)
            else if (pressDuration >= 2000 && currentStep < 2) {
                currentStep = 2;
                IndicateMode(UI_DURATION, currentStep);
            } 
      // 1秒経過 (Step 1)
            else if (pressDuration >= 1000 && currentStep < 1) {
                currentStep = 1;
                IndicateMode(UI_DURATION, currentStep);
            }

      // ループの中で「今のボタンの状態」を再取得する
            currentBtn = GetButtonState();

      // CPU負荷軽減とチャタリング防止
            delay(10); 
        }

    // 3. ループを抜けた＝ボタンが離された瞬間のステップを確定
        selectedMode = currentStep;
    }

    lastBtn = currentBtn;
    return selectedMode;
}


/**
 * @brief 記録終了判定
 * @details 
 * - Modeボタン押しで終了判定
 * @return ...true:終了 / false:継続
 */
bool CheckProcessEnd(){
    int btnState = GetButtonState();
    
    if( btnState == 1 || btnState == 4 ) return true;
    else return false;
}


/**
 * @brief タイムアウトをチェックし、時間切れなら通知を行う
 * @param setTime : タイムアウト時間[ms]
 * @return true: タイムアウト発生 / false: 入力あり（継続）
 */
bool CheckAndHandleTimeOut(unsigned long setTime) {
    if (JudgeTimeOut(setTime)) {
        IndicateMode(UI_DURATION_ERR, 1); // タイムアウトの合図
        return true;
    }
    return false;
}


/**
 * @brief Mode処理のタイムアウトを検知する処理
 * @param setTime : タイムアウト時間[ms]
 * @return true: 時間切れ / false: 時間内に入力あり
 */
 bool JudgeTimeOut( unsigned long setTime ){
    unsigned long startTime = millis();
    int currentBtn = 0;
    
    while(true){
        // --- 1. 入力チェック ---
        currentBtn = GetButtonState();
        
        // ボタン(1〜6) または パドルに入力があった場合
        if( (1 <= currentBtn && currentBtn <= 6) || 
            !digitalRead( PIN_DOT ) || !digitalRead( PIN_DASH ) ) {
            return false; // 入力あり：タイムアウトせず即座に終了
        }
    
        // --- 2. タイムアウト判定 ---
        if( millis() - startTime >= setTime ){
            return true; // 時間切れ
        }

        delay(10); // 10ms待機してループ（チャタリング安定も兼ねる）
    }
}


// --- 出力系 ---
/**
 * @brief Mode番号を知らせるUI処理
 * @details 
 * - Mode Tx : LED点滅
 * - Mode Tone : LED点滅 + Tone音
 * @param duration : 点灯・吹鳴継続時間[ms]
 * @param signalCount : 点滅・吹鳴回数）
 * @return なし
 */
 void IndicateMode( unsigned long duration, int signalCount ){
    do{
        digitalWrite( PIN_LED, HIGH );
        if( !g_isTxMode ) tone( PIN_TX_TONE, g_currentToneFreq );
        delay( duration );
        
        digitalWrite( PIN_LED, LOW );
        if( !g_isTxMode ) noTone( PIN_TX_TONE );
        delay( duration );

        signalCount--;
    }while( signalCount );
}


/**
 * @brief 現在記憶のKey Speed(WPM) をアナウンス
 * @details
 *  - 
 * @param wpm : 現在のKeySpeed
 * @return なし
 */
void AnnounceWpm( byte wpm ){
    static const byte numCode[]={
        0b00111111, // 0
        0b00101111, // 1
        0b00100111, // 2
        0b00100011, // 3
        0b00100001, // 4
        0b00100000, // 5
        0b00110000, // 6
        0b00111000, // 7
        0b00111100, // 8
        0b00111110};// 9

        byte ten = wpm / 10; //十の位
        byte one = wpm % 10; //一の位
        
        SendByCode( numCode[ten] );
        SendByCode( numCode[one] );
}


// ==========================================
// 6-4. エレキー中核ロジック (Iambic MEMO B)
// ==========================================
/* パドル入力を符号（Dot/Dash）として送出・記録する心臓部 */
/**
 * @brief Dot処理（短点）
 * @details
 *  - エレキーメイン処理とEEPROMメモリ内容も作成できる 
 * @param isRecord : メモリ記録フラグ（trueで記録有効）
 */
void ProcessDot(bool isRecord) {
    unsigned long startTime = millis();
  
    KeyOn(); // 吹鳴開始

  // 出力中のスクイズ監視
    do {
        if (!digitalRead(PIN_DASH)) g_flgDash = true;
    } while (millis() <= startTime + g_durationDot);

  // --- オリジナルのビット詰め記録ロジック ---
    if (isRecord) {
        g_workingMemo.code[g_workingMemo.numOfCode] <<= 1;
        g_workingMemo.code[g_workingMemo.numOfCode] &= 0xFE; // 末尾0
    }

    KeyOff(); // 吹鳴終了
  
    delay(g_durationDot); // 符号間スペース
    g_flgDot = false; 

    if (g_flgDash) ProcessDash(isRecord);
}


/**
 * @brief Dash処理（長点）
 *  - エレキーメイン処理とEEPROMメモリ内容も作成できる 
 * @param isRecord : メモリ記録フラグ（trueで記録有効）
 */
void ProcessDash(bool isRecord) {
    unsigned long startTime = millis();
  
    KeyOn();

  // 出力中のスクイズ監視
    do {
        if (!digitalRead(PIN_DOT)) g_flgDot = true;
    } while (millis() <= startTime + g_durationDot * 3);

  // --- オリジナルのビット詰め記録ロジック ---
    if (isRecord) {
        g_workingMemo.code[g_workingMemo.numOfCode] <<= 1;
        g_workingMemo.code[g_workingMemo.numOfCode] |= 0x01; // 末尾1
    }

    KeyOff();
  
    delay(g_durationDot);
    g_flgDash = false; 

    if (g_flgDot) ProcessDot(isRecord);
}


// ==========================================
// 6-5. メモリ・EEPROM制御層
// ==========================================
/* 符号データの保存、読み出し、再生に関する処理 */
// --- 記録系 ---
/**
 * @brief メモリー記録処理
 * @details 
 * - 各チャネルに打鍵して符号を記録(文字数max:MEMO_CODE_MAX)
 * @param targetCh : 対象CH番号 1 or 2
 * @return isRecordSuccess...true:正常記録 / false:文字オーバーなど
 */
bool RecordToMemory( int targetCh ){
    bool isRecordSuccess = false;   //記録成功・不成功状態
    bool flgEnd = false;
    
    int idx = targetCh - 1; //CH番号と配列処理時のインデックス合わせ
    
// --- 記録開始前の初期化 ---
    for (int j = 0; j < MEMO_CODE_MAX; j++) {
      g_workingMemo.code[j] = 0x01; // スタートビットで初期化
    }
    g_workingMemo.numOfCode = 0;

    unsigned long startTimeEmpty = 0;
    int statusWait = 0;
    
    delay(200); //チャンネル選択ボタンを離す時間を確保し、即終了を防ぐ

// --- 記録符号生成 ---
    do {
        if (!digitalRead(PIN_DOT) || !digitalRead(PIN_DASH)) {
        // 入力があった場合
            if (!digitalRead(PIN_DOT))  ProcessDot(true);
            if (!digitalRead(PIN_DASH)) ProcessDash(true);
        
        // 待機状態のリセット
            startTimeEmpty = 0;
            statusWait = 0;
        } else {
        // パドルが押されていない時間による判定
            if (startTimeEmpty == 0) startTimeEmpty = millis();

        // 1. 文字間隔（短点1つ分）経過：次のバイトへ
            if ((millis() > startTimeEmpty + g_durationDot) && statusWait == 0) {
                statusWait = 1;
                g_workingMemo.numOfCode++;
            }
        // 2. 単語間隔（短点4つ分）経過：さらに次のバイト（空白用）へ
            if ((millis() >= startTimeEmpty + g_durationDot * 4) && statusWait == 1) {
                statusWait = 2;
                g_workingMemo.numOfCode++;
            }
        }
      
      // 記憶終了判定
        if( CheckProcessEnd() ){
            flgEnd = true;
            isRecordSuccess = true;
        }

      // 配列のオーバーフローガード
        if (g_workingMemo.numOfCode >= MEMO_CODE_MAX) {
            flgEnd = true;
            isRecordSuccess = false;
        }

    } while ( !flgEnd );        

//EEPROM書き込み、g_currentMemo書き換え
    if( isRecordSuccess ){
// --- EEPROMへの書き込み ---
        int targetAddr = (idx == 0) ? ADDR_MEMO_0 : ADDR_MEMO_1; // 保存するチャネルのアドレスを計算
        EEPROM.put(targetAddr, g_workingMemo);  // 構造体の中身をまるごとEEPROMに書き込む           

// --- currentMemoの対象CH内容を書き換え ---
        g_currentMemo[idx] = g_workingMemo;
    }
    
    return isRecordSuccess;
}


// --- 再生系 ---
/**
 * @brief メモリ内容を連続再生する処理
 * @param targetCh : 対象CH番号 1 or 2
 * @return なし
 */
void PlayFromMemory(int targetCh) {
    int idx = targetCh - 1; // 1,2 -> 0,1
    
    // 記録されている符号数分ループ
    for (int i = 0; i < g_currentMemo[idx].numOfCode; i++) {
        
        // --- 再生中断のチェック ---
        // Modeボタンが押された、またはパドル(Dot/Dash)が操作された場合
        if (CheckProcessEnd() || !digitalRead(PIN_DOT) || !digitalRead(PIN_DASH)) {
            return; // 音を出さずに即座に戻る
        }

        // 1バイト（一文字分）を再生
        // SendByCodeの中断フラグをチェック
        if (SendByCode(g_currentMemo[idx].code[i])) return;
    }
}


/**
 * @brief メモリ内容を符号毎に再生していく処理
 * @param code : 再生する符号（一文字、または空白用 0x01）
 * @return bool : trueなら中断、falseなら完了
 */
bool SendByCode(byte code) { // void から bool に変更
  // --- 1. 空白（Word Space）の判定 ---
  // codeが0x01のときは単語の間隔（合計7 dot）を作る
  if (code == 0x01) {
    // PlayElement末尾の1 + SendByCode末尾の2 + ここでの4 = 合計7 dot
    delay(g_durationDot * 4);
    return false;
  }

  // --- 2. スタートビットの検出 ---
  int startBitPos = 0;

  // 0x80 (B10000000) を右にシフトしながら、最初に 1 が現れる位置を探す
  while (!(code & (0x80 >> startBitPos))) {
    startBitPos++;
    // 安全策：1が見つからないまま8ビット超えたら終了
    if (startBitPos >= 8) return false; 
  }

  // --- 3. 信号の再生ループ ---
  // スタートビットの「次のビット」から処理を開始するため、
  // ループの開始値を k = startBitPos + 1 に設定する
  for (int k = startBitPos + 1; k < 8; k++) {
    // ★追加：PlayElementの結果を見て、中断なら即座にリターン
    bool isInterrupt = false;
    // 0x80を基準に、k回右シフトして現在のビットをチェック
    if (code & (0x80 >> k)) {
      // ビットが1なら長点 (3 dot)
      isInterrupt = PlayElement(g_durationDot * 3);
    } else {
      // ビットが0なら短点 (1 dot)
      isInterrupt = PlayElement(g_durationDot);
    }

    if (isInterrupt) return true;
  }

  // --- 4. 文字間隔（Character Space）の調整 ---
  // 文字が終わるごとに合計 3 dotの休みを入れる
  // PlayElement の最後に 1 dot あるので、ここで +2 dot して合計 3
  delay(g_durationDot * 2);
  return false;
}


/**
 * @brief DOT/DASHを実際に再生（LED点滅＆吹鳴）
 * @param duration : 点灯＆吹鳴継続時間[ms]
 * @return bool : trueなら中断要請あり
 */
bool PlayElement(int duration){
    // ★追加：エレメント開始前に割り込みチェック
    if (CheckProcessEnd() || !digitalRead(PIN_DOT) || !digitalRead(PIN_DASH)) return true;

    KeyOn();
    delay(duration);        //Dot or Dash時間
  
    KeyOff();
    delay(g_durationDot);   //Dot,Dash-Dot,Dash間隔

    // ★追加：エレメント終了直後に割り込みチェック
    if (CheckProcessEnd() || !digitalRead(PIN_DOT) || !digitalRead(PIN_DASH)) return true;
    
    return false;
}


// ==========================================
// 6-6. 設定・システム管理層
// ==========================================
/* デバイスの動作パラメータ（WPM、周波数等）を変更・管理する層 */
/**
 * @brief Key Speed (WPM) を調整 & EEPROM保存
 * @details
 *  - g_currentWpmをベースにCH1:+ / CH2:-
 *  - パドル介入で強制終了
 *  - 一時的に変更 or デフォルトとして記憶
 *  - isRecord : true...現状記憶：Modeボタンを押す
 *  -                   変更時は変更後にModeボタンを押す
 * @param isRecord ... true:記憶あり、false:記憶なし
 * @return なし
 */
void ChangeKeySpeed( bool isRecord ) {
    byte workingWpm = g_currentWpm;
    unsigned long working_durationDot = g_durationDot;
    
    while (true) {
        // 5秒間、ボタンもパドルも操作されなければエラー音を出して終了
        if (CheckAndHandleTimeOut(5000)) break;

        int btn = GetButtonState();

        // --- 1. スピード調整 (CH1:+, CH2:-) ---
        if (btn == 2 || btn == 5) { 
                if (workingWpm < WPM_MAX) workingWpm++;
                working_durationDot = CalcDotTime(workingWpm);
                IndicateMode(UI_DURATION, 1); 
                while (GetButtonState() == btn); // 今押したボタンと同じ値である限りループ
        }
        
        if (btn == 3 || btn == 6) { 
                if (workingWpm > WPM_MIN) workingWpm--;
                working_durationDot = CalcDotTime(workingWpm);
                IndicateMode(UI_DURATION, 1);
                while (GetButtonState() == btn); // 今押したボタンと同じ値である限りループ
        }

        // --- 2. モード確定（Modeボタン） ---
        if (CheckProcessEnd()) {
            g_currentWpm = workingWpm;
            g_durationDot = working_durationDot;
            
            if( isRecord ){
                EEPROM.update(ADDR_MEMO_WPM, workingWpm);
                IndicateMode(UI_DURATION, 3);                 
            }
            else IndicateMode(UI_DURATION, 1);
            
            break;
        }

        // --- 3. 強制終了（パドル介入） --- 符号送出の邪魔をしないよう、合図なしで即終了
        if (!digitalRead(PIN_DOT) || !digitalRead(PIN_DASH)) break;
    }
}
