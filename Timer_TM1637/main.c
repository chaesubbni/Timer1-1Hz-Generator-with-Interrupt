#define F_CPU 16000000UL
#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>

#define CLK_PIN 2
#define DIO_PIN 3

// 메인 루프와 인터럽트 간 공유 변수 (반드시 volatile 선언)
volatile unsigned int display_value = 0;
volatile unsigned char update_flag = 1; // 시작하자마자 0을 띄우기 위해 1로 초기화

// Flash 메모리에 저장되는 FND 출력 테이블
const unsigned char digit_table[] = {
	0x3F, // 0
	0x06, // 1
	0x5B, // 2
	0x4F, // 3
	0x66, // 4
	0x6D, // 5
	0x7D, // 6
	0x07, // 7
	0x7F, // 8
	0x6F  // 9
};

// ==========================================
// 1. TM1637 하위 통신 함수 (Bit-banging)
// ==========================================

void tm1637_start(void) {
	// CLK가 High인 상태에서 DIO를 Low로 내리면 Start 조건
	DDRD |= (1 << DIO_PIN); // DIO 출력 모드 (0V)
	_delay_us(5);
}

void tm1637_stop(void) {
	// CLK가 High일 때 DIO가 Low에서 High로 올라가면 Stop 조건
	PORTD &= ~(1 << CLK_PIN); // CLK Low
	DDRD |= (1 << DIO_PIN);   // DIO Low
	_delay_us(5);
	
	PORTD |= (1 << CLK_PIN);  // CLK High
	_delay_us(5);
	
	DDRD &= ~(1 << DIO_PIN);  // DIO High (입력 모드 전환으로 외부 풀업)
	_delay_us(5);
}

void tm1637_write_byte(unsigned char data) {
	// 8비트 데이터 전송
	for (unsigned char i = 0; i < 8; i++) {
		PORTD &= ~(1 << CLK_PIN); // CLK Low
		
		if ((data >> i) & 0x01) {
			DDRD &= ~(1 << DIO_PIN); // 1: 입력 모드 (High-Z -> 외부 풀업 5V)
			} else {
			DDRD |= (1 << DIO_PIN);  // 0: 출력 모드 (GND 스위치 ON -> 0V)
		}
		_delay_us(5);
		
		PORTD |= (1 << CLK_PIN); // CLK High (TM1637이 데이터 래치)
		_delay_us(5);
	}
	
	// 9번째 클럭: ACK 수신 및 무시 (타이밍만 맞춤)
	PORTD &= ~(1 << CLK_PIN); // CLK Low
	DDRD &= ~(1 << DIO_PIN);  // MCU는 DIO 입력 모드로 전환하여 핀을 놔줌
	_delay_us(5);
	
	PORTD |= (1 << CLK_PIN); // CLK High
	_delay_us(5);
}

// ==========================================
// 2. TM1637 상위 제어 함수
// ==========================================

void tm1637_display(unsigned int num) {
	// 4자리 숫자를 각 자리수별로 분리하여 패턴 매핑
	unsigned char digits[4];
	digits[0] = digit_table[(num / 1000) % 10];
	digits[1] = digit_table[(num / 100) % 10];
	digits[2] = digit_table[(num / 10) % 10];
	digits[3] = digit_table[num % 10];

	// Command 1: 데이터 기록 모드, 자동 주소 증가
	tm1637_start();
	tm1637_write_byte(0x40);
	tm1637_stop();

	// Command 2: 첫 번째 주소(0xC0) 설정 후 연속 4바이트 전송
	tm1637_start();
	tm1637_write_byte(0xC0);
	for (int i = 0; i < 4; i++) {
		tm1637_write_byte(digits[i]);
	}
	tm1637_stop();

	// Command 3: 디스플레이 ON 및 밝기 설정 (0x8F = 최대 밝기)
	tm1637_start();
	tm1637_write_byte(0x8F);
	tm1637_stop();
}

// ==========================================
// 3. 타이머 및 인터럽트
// ==========================================

void timer1_init(void) {
	TCCR1A = 0x00; // 일반 포트 동작
	
	// CTC 모드(WGM12=1), 분주비 1024 (CS12=1, CS10=1)
	TCCR1B |= (1 << WGM12) | (1 << CS12) | (1 << CS10);
	
	// 16MHz / 1024 = 15625Hz. 1초를 만들기 위한 비교값: 15625 - 1 = 15624
	OCR1A = 15624;
	
	// Timer1 Compare Match A 인터럽트 활성화
	TIMSK1 |= (1 << OCIE1A);
}

ISR(TIMER1_COMPA_vect) {
	display_value++; // 1초마다 값 증가
	if (display_value > 9999) {
		display_value = 0; // 4자리를 넘어가면 리셋
	}
	update_flag = 1; // 메인 루프에 업데이트 지시
}

// ==========================================
// 4. 메인 함수
// ==========================================

int main(void) {
	// I/O 핀 초기화
	PORTD |= (1 << CLK_PIN);
	PORTD &= ~(1 << DIO_PIN); // 내부 풀업 끄기
	DDRD |= (1 << CLK_PIN);
	DDRD &= ~(1 << DIO_PIN);  // DIO 핀 입력(High-Z) 상태로 시작
	_delay_us(5);
	
	timer1_init(); // 하드웨어 타이머 설정
	sei();         // 글로벌 인터럽트 활성화

	while(1) {
		// 인터럽트에서 플래그가 켜졌을 때만 I/O 통신 실행
		if (update_flag) {
			update_flag = 0; // 플래그 초기화
			tm1637_display(display_value); // 계산된 딜레이 기반의 Custom 함수 실행
		}
	}
}