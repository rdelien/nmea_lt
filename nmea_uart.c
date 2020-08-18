#include <xc.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>


/******************************************************************************/
/* Macros                                                                     */
/******************************************************************************/
#define _XTAL_FREQ 32000000

#define RXBUFFER			/* Use buffers for received characters */
//#define TXBUFFER			/* Use buffers for transmitted character (MAKE SURE TO ENABLE INTERRUPTS BEFORE TRANSMITTING ANYTHING) */
#define BUFFER_SIZE		8	/* Buffer size. Has to be a power of 2 and congruent with queue.head and queue.tail or roll-overs need to be taken into account */
#define BUFFER_SPARE		2	/* Minumum number of free positions before issuing Xoff */

#define INTDIV(n,d)             ((n)+((((n)>=0&&(d)>=0)||((n)<0&&(d)<0))?((d)/2):-((d)/2)))/(d)  /* Macro for integer division with proper round-off (BEWARE OF OVERFLOW!) */
#define FREE(h,t,s)		(((h)>=(t))?((s)-((h)-(t))-1):((t)-(h))-1)

#define XON			0x11	/* ASCII value for Xon (^S) */
#define XOFF			0x13	/* ASCII value for Xoff (^Q) */


/******************************************************************************/
/* Types                                                                      */
/******************************************************************************/
struct queue {
	char		buffer[BUFFER_SIZE];	/* Here's where the data goes */
	unsigned	head		: 3;	/* Index to a currently free position in buffer */
	unsigned	tail		: 3;	/* Index to the oldest occupied position in buffer, if not equal to head */
	unsigned	xon_enabled	: 1;	/* Specifies if Xon/Xoff should be issued/adhered to */
	unsigned	xon_state	: 1;	/* Keeps track of current Xon/Xoff state for this queue */
};


/******************************************************************************/
/* Global data                                                                */
/******************************************************************************/
#ifdef RXBUFFER
static struct queue		rx;
#endif /* RXBUFFER */
#ifdef TXBUFFER
static struct queue		tx;
#endif /* TXBUFFER */


/******************************************************************************/
/* Functions                                                                  */
/******************************************************************************/
void nmea_uart_init(unsigned long bitrate, unsigned char flow)
{
#ifdef RXBUFFER
	rx.head        = 0;
	rx.tail        = 0;
	rx.xon_enabled = flow;
	rx.xon_state   = 1;
#endif /* RXBUFFER */

#ifdef TXBUFFER
	tx.head        = 0;
	tx.tail        = 0;
	tx.xon_enabled = flow;
	tx.xon_state   = 1;
#endif /* TXBUFFER */

	BAUD1CONbits.BRG16 = 1;	/* Use 16-bit bit rate generation */
	if (BAUD1CONbits.BRG16) {
		/* 16-bit bit rate generation */
		unsigned long	divider = INTDIV(INTDIV(_XTAL_FREQ, 4UL), bitrate)-1;

		SP1BRGL =  divider & 0x00FF;
		SP1BRGH = (divider & 0xFF00) >> 8;
	} else
		/* 8-bit bit rate generation */
		SPBRG = INTDIV(INTDIV(_XTAL_FREQ, bitrate), 16UL)-1;

	TX1STAbits.CSRC = 1;	/* Clock source from BRG */
	TX1STAbits.BRGH = 1;	/* High-speed bit rate generation */
	TX1STAbits.SYNC = 0;	/* Asynchronous mode */
	RC1STAbits.SPEN = 1;	/* Enable serial port pins */
	RC1IE           = 0;	/* Disable rx interrupt */
	TX1IE           = 0;	/* Disable tx interrupt */
	RC1STAbits.RX9  = 0;	/* 8-bit reception mode */
	TX1STAbits.TX9  = 0;	/* 8-bit transmission mode */
	RC1STAbits.CREN = 0;	/* Reset receiver */
	RC1STAbits.CREN = 1;	/* Enable reception */
	TX1STAbits.TXEN = 0;	/* Reset transmitter */
	TX1STAbits.TXEN = 1;	/* Enable transmission */

#ifdef RXBUFFER
	RC1IE = 1;	/* Enable rx interrupt */

	if (rx.xon_enabled) {
		TX1REG = XON;
		rx.xon_state = 1;
	}
#endif /* RXBUFFER */
}


void nmea_uart_term(void)
{
#ifdef RXBUFFER
	RC1IE = 0;	/* Disable rx interrupt */
#endif /* RXBUFFER */
#ifdef TXBUFFER
	while (tx.head != tx.tail);
	TX1IE = 0;	/* Disable tx interrupt */
#endif /* TXBUFFER */

#ifdef RXBUFFER
	if (rx.xon_enabled && rx.xon_state) {
		while(!TX1IF);
		TX1REG = XOFF;
		rx.xon_state = 0;
	}
#endif /* RXBUFFER */
	while(!TX1IF);	/* Wait for the last character to go */

	RC1STAbits.CREN = 0;	/* Disable reception */
	TX1STAbits.TXEN = 0;	/* Disable transmission */
}


void nmea_uart_rx_isr(void)
{
#ifdef RXBUFFER
	/* Handle overflow errors */
	if (RC1STAbits.OERR) {
		rx.buffer[rx.head] = RC1REG; /* Read RX register, but do not queue */
		TX1STAbits.TXEN = 0;
		TX1STAbits.TXEN = 1;
		RC1STAbits.CREN = 0;
		RC1STAbits.CREN = 1;
		return;
	}
	/* Handle framing errors */
	if (RC1STAbits.FERR) {
		rx.buffer[rx.head] = RC1REG; /* Read RX register, but do not queue */
		TX1STAbits.TXEN = 0;
		TX1STAbits.TXEN = 1;
		return;
	}
	/* Copy the character from RX register into RX queue */
	rx.buffer[rx.head] = RC1REG;
#ifdef TXBUFFER
	/* Check if an Xon or Xoff needs to be handled */
	if (tx.xon_enabled) {
		if (tx.xon_state && (rx.buffer[rx.head] == XOFF)) {
			tx.xon_state = 0;
			TX1IE = 0;	/* Disable tx interrupt to stop transmitting */
			return;
		} else if (!tx.xon_state && (rx.buffer[rx.head] == XON)) {
			tx.xon_state = 1;
			/* Enable tx interrupt if tx queue is not empty */
			if (tx.head != tx.tail)
				TX1IE = 1;
			return;
		}
	}
#endif /* TXBUFFER */
	/* Queue the character */
	rx.head++;
	/* Check if an Xoff is in required */
	if (rx.xon_enabled &&
	    rx.xon_state &&
	    (FREE(rx.head, rx.tail, BUFFER_SIZE) <= (BUFFER_SPARE))) {
		while(!TX1IF);	// TBD: Could potentially wait forever here
		TX1REG = XOFF;
		rx.xon_state = 0;
	}
	/* Check for an overflow */
	if (rx.head == rx.tail)
		/* Dequeue the oldest character */
		rx.tail++;
#endif /* RXBUFFER */
}


void nmea_uart_tx_isr(void)
{
#ifdef TXBUFFER
	/* Copy the character from the TX queue into the TX register */
	TX1REG = tx.buffer[tx.tail];
	/* Dequeue the character */
	tx.tail++;

	/* Disable tx interrupt if queue is empty */
	if (tx.head == tx.tail)
		TX1IE = 0;
#endif /* TXBUFFER */
}


static void name_uart_putch(char ch)
{
#ifdef TXBUFFER
	unsigned char	queued = 0;

	do {
#ifdef RXBUFFER
		RC1IE = 0;	/* Disable rx interrupt for concurrency */
#endif /* RXBUFFER */
		TX1IE = 0;	/* Disable tx interrupt for concurrency */

		/* Check if there's room in the queue ((tx.head + 1) != tx.tail won't work due to bitfields being cast) */
		if (FREE(tx.head, tx.tail, BUFFER_SIZE)) {
			/* Copy the character into the TX queue */
			tx.buffer[tx.head] = ch;
			/* Queue the character */
			tx.head++;
			queued = 1;
		} else
			CLRWDT();
		if (!tx.xon_enabled || tx.xon_state)
			TX1IE = 1;	/* Re-enable tx interrupt */
#ifdef RXBUFFER
		RC1IE = 1;	/* Re-enable rx interrupt */
#endif /* RXBUFFER */
	} while (!queued);
#else
	if (!RC1STAbits.SPEN)
		return;
	while(!TX1IF) {	/* Wait for TX1REG to be empty */
		if (RC1STAbits.OERR) {
			TX1STAbits.TXEN = 0;
			TX1STAbits.TXEN = 1;
			RC1STAbits.CREN = 0;
			RC1STAbits.CREN = 1;
		}
		if (RC1STAbits.FERR) {
			volatile unsigned char dummy;

			dummy = RC1REG;
			TX1STAbits.TXEN = 0;
			TX1STAbits.TXEN = 1;
		}
		CLRWDT();
	}
	TX1REG = ch;
	CLRWDT();
#endif /* TXBUFFER */
}


static char name_uart_getch(void)
{
#ifdef RXBUFFER
	char  result = EOF;

	RC1IE = 0;	/* Disable rx interrupt for concurrency */
#ifdef TXBUFFER
	TX1IE = 0;	/* Disable tx interrupt for concurrency */
#endif /* TXBUFFER */

	/* Check if there's anything to read */
	if (rx.head != rx.tail) {
		/* Copy the character from the RX queue */
		result = rx.buffer[rx.tail];
		/* Dequeue the character */
		rx.tail++;
		/* Check if an Xon is in required */
		if (rx.xon_enabled &&
		    !rx.xon_state &&
		    (rx.head == rx.tail)) {
			while(!TX1IF);
			TX1REG = XON;
			rx.xon_state = 1;
		}
	}

	RC1IE = 1;	/* Re-enable rx interrupt */
#ifdef TXBUFFER
	if ((tx.head != tx.tail) && (!tx.xon_enabled || tx.xon_state))
		TX1IE = 1;	/* Re-enable tx interrupt */
#endif /* TXBUFFER */

	return result;

#else /* !RXBUFFER */
	if (!RCIF)
		return EOF;

	if (RC1STAbits.OERR) {
		TX1STAbits.TXEN = 0;
		TX1STAbits.TXEN = 1;
		RC1STAbits.CREN = 0;
		RC1STAbits.CREN = 1;
		return 0;
	}
	if (RC1STAbits.FERR) {
		volatile unsigned char dummy;

		dummy = RC1REG;
		TX1STAbits.TXEN = 0;
		TX1STAbits.TXEN = 1;
		return 0;
	}

	return RC1REG;
#endif /* RXBUFFER */
}

static void proc_nmea(int argc, char *argv[])
{
	if (!strcmp(argv[0], "GPRMC")) {
		printf("time: '%s'\n", argv[1]);
		printf("date: '%s'\n", argv[9]);
	} else
		printf("NMEA: unsupported sentence '%s'\n", argv[0]);
}


#define NMEA_ARGS_MAX  16
static void proc_nmea_sentence(char *sentence, char len)
{
	unsigned char  ndx;
	char           *endptr;
	unsigned long  checksum;
	unsigned char  calcsum = 0;
	int            argc = 0;
	char           *argv[NMEA_ARGS_MAX];

	if (len < 4)
		/* NMEA sentence too short */
		return;
	if (sentence[len - 3] != '*')
		/* Checksum separator missing */
		return;

	checksum = strtoul(&sentence[len - 2], &endptr, 16);
	if (*endptr != '\0')
		/* Trailing garbage */
		return;

	for (ndx = 0; ndx < len - 3; ndx++)
		calcsum ^= sentence[ndx];
	if (calcsum != checksum)
		return;
	sentence[len - 3] = '\0';
	len -= 3;

	/* Build argument list */
	while (*sentence != '\0') {
		/* Replace leading commas with 0-terminations and add an empty argument for each one */
		while (*sentence == ',') {
			*sentence = '\0';
			argv[argc] = sentence;
			argc++;
			sentence++;
		}

		/* Store the beginning of this argument */
		if (*sentence != '\0') {
			argv[argc] = sentence;
			argc++;
		}

		/* Skip characters until past argument (indicated by a separating comma or a 0-termination) */
		while (*sentence != '\0' &&
		       *sentence != ',') {
			sentence++;
		}

		/* If the argument was termintated by a separating comma, replace is with a 0-termination */
		if (*sentence == ',') {
			*sentence = '\0';
			sentence++;
		}
	}

	proc_nmea(argc, argv);
}


static void proc_nmea_char(char byte)
{
	static char           sentence[82 + 1];
	static char           len;
	static unsigned char  receiving = 0;

	/* Test if we need to start receiving */
	if (!receiving) {
		switch (byte) {
		default:
		case '\r':
			printf("NMEA: Discarding OOB byte 0x%.2x\n", byte);
		case '\n':
			return;

		case '$':
			receiving = 1;
			len = 0;
		}
	}

	/* Test if we need to end receiving */
	if (byte == '\n' ||
	    byte == '\r') {
		receiving = 0;
		sentence[len] = '\0';
		proc_nmea_sentence(&sentence[1], len - 1);
		return;
	}

	/* Copy the received byte into the current received sentence */
	sentence[len] = byte;

	if (++len > 81) {
		receiving = 0;
		sentence[len] = '\0';
		printf("NMEA: Discarding over-sized sentence '%s'\n", sentence);		
	}
}


void nmea_work(void)
{
	char  byte;

	while ((byte = name_uart_getch()) != (char)EOF)
		proc_nmea_char(byte);
}
