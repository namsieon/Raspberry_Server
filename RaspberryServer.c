#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>     // IPv4 전용 기능 사용
#include <arpa/inet.h>      // 주소변환 기능 사용
#include <unistd.h>         // tcp,ip 통신에서 read,write 사용 가능
#include <errno.h>
#include <fcntl.h>
#include <softPwm.h>
#include <wiringPi.h>       // pinMode와 OUTPUT 사용가능
#include <wiringPiSPI.h>

#define PORT_NUMBER             7777
#define BUF_SIZE                1024

#define MAXTIMINGS              85
#define MAX_VALUE               99

#define CS_MCP3008              8                   // BCM 번호

#define DHTPIN                  7					// 물리핀 번호 혹은 WiringPi 핀 번호

#define LIGHT_PWM               18                  // BCM번호
#define LIGHT_PWM_PI            1                   // LIGHT PWM용 번호

#define SPI_CHANNEL             0
#define SPI_SPEED               1000000             // 1MHz

char smart_mode = FALSE;                            // 스마트모드 설정

int dht11_dat[5] = { 0, };                          // 센서값
char temper[10]={0,};                               // 온도 저장 변수
char hum[10]={0,};                                  // 습도 저장 변수
char th[20]={0,};                                   // 온습도 저장

void GPIO_Set(){
	pinMode(LIGHT_PWM,OUTPUT);
	softPwmCreate(LIGHT_PWM_PI, 0, MAX_VALUE);
	pinMode(CS_MCP3008, OUTPUT);
}

void Set_PWM(int PulseWidth){
    softPwmWrite(LIGHT_PWM_PI,PulseWidth);
}

int read_mcp3008_adc(unsigned char adcChannel)      // ADC값을 읽어주는 함수
{
    unsigned char buff[3];
    int adcValue = 0;

    buff[0] = 0x06 | ((adcChannel & 0x07) >> 2);
    buff[1] = ((adcChannel & 0x07) << 6);
    buff[2] = 0x00;

    digitalWrite(CS_MCP3008, 0);  // Low : CS Active

    wiringPiSPIDataRW(SPI_CHANNEL, buff, 3);

    buff[1] = 0x0F & buff[1];
    adcValue = ( buff[1] << 8) | buff[2];

    digitalWrite(CS_MCP3008, HIGH);  // High : CS Inactive

    return adcValue;
}

int read_dht11_dat(){
    char laststate           = HIGH;
    unsigned char counter    = 0;
    unsigned char j          = 0, i;
    dht11_dat[0] = dht11_dat[1] = dht11_dat[2] = dht11_dat[3] = dht11_dat[4] =0;
    pinMode( DHTPIN, OUTPUT );
    digitalWrite( DHTPIN, LOW );
    delay( 18 );
    digitalWrite( DHTPIN, HIGH );
    delayMicroseconds( 40 );
    pinMode( DHTPIN, INPUT );
    for ( i =0; i < MAXTIMINGS; i++ ){
        counter =0;
        while ( digitalRead( DHTPIN ) == laststate ){
            counter++;
            delayMicroseconds( 1 );
            if (counter == 255){
                break;
            }
        }
        laststate = digitalRead( DHTPIN );
        if ( counter ==255 )
            break;
        if ( (i >=4) && (i % 2==0) ){
            dht11_dat[j /8] <<=1;
            if ( counter >50 )
                dht11_dat[j /8] |=1;
            j++;
        }
    }

    if ( (j >=40) && (dht11_dat[4] == ( (dht11_dat[0] + dht11_dat[1] + dht11_dat[2] + dht11_dat[3]) &0xFF) ) ){
        return 0;                     // 오류없음
    }else  {
        return -1;            // 오류발생. 재시도
    }
}

void check_dht11_dat(){
    char tmp_t[5]={0,};
    char tmp_h[5]={0,};
    int state=0;
    do{
        printf("온/습도 파악 중...\n");
        state = read_dht11_dat();
        delay(2000);
    }while(state!=0);
    // 온습도 센서를 정상적으로 받을 때까지 반복

    sprintf(temper, "%d", dht11_dat[0]);
    sprintf(tmp_t, "%d", dht11_dat[1]);
    strcat(temper,".");
    strcat(temper,tmp_t);

    sprintf(temper, "%d", dht11_dat[2]);
    sprintf(tmp_h, "%d", dht11_dat[3]);
    strcat(temper,".");
    strcat(temper,tmp_h);
}

void Bluetooth_Ctrl(char* msg){
    int fd = open("/dev/rfcomm0", O_WRONLY, 0777);
    write(fd,msg,strlen(msg));
    close(fd);
}

int main(void){
    int listenfd = 0, connfd = 0;
    int adcChannel=0;                                   // ADC값의 채널 0번 의미
    int adc_bright=0;                                   // 실내 조명의 밝기 값
    struct sockaddr_in serv_addr;

    char receiveBuff[BUF_SIZE];
    char sendBuff[BUF_SIZE];

    char pwm_data[3];

    int size=sizeof(pwm_data)/sizeof(char);
    int brightness=0;

    if(wiringPiSetup() == -1){
        printf("WiringPi핀 초기화 오류\n");
        exit(1);
    }

    if(wiringPiSPISetup(SPI_CHANNEL, SPI_SPEED) == -1){
        printf("wiringPiSPI통신 초기화 오류\n");
        exit(1);
    }
    
    GPIO_Set();

    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if(listenfd == -1)
    {
        printf("server socket 생성 실패\n");
        exit(1);
    }

    memset(&serv_addr, '0', sizeof(serv_addr));                     //set default value '0'

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(PORT_NUMBER);

    if( bind(listenfd, (struct sockaddr*) &serv_addr, sizeof(serv_addr))== -1 )
    {
        printf("bind() 실행 에러\n");
        exit(1);
    }

    if( listen(listenfd, 10) == -1 )
    {// 대기메시지 큐 개수 10개
            printf("listen() 실행 실패\n");
            exit(1);
    }
    printf("IoT Server가 가동되었습니다.\n");

    while(TRUE){
        memset(receiveBuff, 0 ,sizeof(receiveBuff));
        memset(sendBuff, 0 ,sizeof(sendBuff));
        memset(pwm_data, 0 ,sizeof(pwm_data));
        memset(th, 0,sizeof(th));

        check_dht11_dat();
        adc_bright=read_mcp3008_adc(adcChannel);
        printf( "Temperature = %d.%d C Humidity = %d.%d %%\n",
        dht11_dat[2], dht11_dat[3], dht11_dat[0], dht11_dat[1]);
        printf("밝기 값(높을수록 밝음) = %d\n", adc_bright);

        connfd = accept(listenfd, (struct sockaddr*) NULL, NULL);
        if(connfd == -1)
        {
            printf("클라이언트 연결 수락 실패\n");
            exit(1);
        }
        read(connfd, receiveBuff, BUF_SIZE);

        if(strlen(receiveBuff)!=0){             // 받은 데이터를 띄워줌
            printf("받은 데이터, 데이터 길이 : %s %d\n", receiveBuff, strlen(receiveBuff));
        }

        if(!strcmp(receiveBuff,"smart.o")){             // 스마트 모드 활성화 요청
            smart_mode=TRUE;
            printf("스마트모드 활성화 요청 감지\n");
            write(connfd,"smart.o",strlen("smart.o"));
        }else if(!strcmp(receiveBuff,"smart.f")){       // 스마트 모드 비활성화 요청
            smart_mode=FALSE;
            printf("스마트모드 비활성화 요청 감지\n");
            write(connfd,"smart.f",strlen("smart.f"));
        }

        //////스마트 모드 on상태//////
        else if(smart_mode){

            if(adc_bright<800){
				brightness = MAX_VALUE;
				Set_PWM(brightness);
				//write(connfd,"OK", strlen("OK"));
				// 클라이언트에 신호를 보내야 함
            }else if(adc_bright>=800 && adc_bright<1200){
				brightness = 50;
				Set_PWM(brightness);
            }else if(adc_bright>=2500){
				brightness = 0;
				Set_PWM(brightness);
            }
        }//////스마트 모드 on 상태//////
		
        else if(!strcmp(receiveBuff,"PW")){// 아두이노에서 보내는 비밀번호 오류 경고
            printf("도어락 비밀번호 입력 실패가 많습니다. 확인이 필요할지도 모릅니다.\n");
        }
        //////// 음성인식 + PC명령 ////////
        else if( !strcmp(receiveBuff,"불 켜") || !strcmp(receiveBuff,"조명 켜")){
            brightness = MAX_VALUE;
            Set_PWM(brightness);
            write(connfd,"OK", strlen("OK"));//PC에서 표시되는 메세지가 되기도 함
        }else if(!strcmp(receiveBuff,"불 꺼") || !strcmp(receiveBuff,"조명 꺼")){
            brightness = 0;
            Set_PWM(brightness);
            write(connfd,"OK", strlen("OK"));//PC에서 표시되는 메세지가 되기도 함
        }
        else if(!strcmp(receiveBuff,"REQA")){////////////////// 모든 디바이스 상태 확인
            sprintf(pwm_data, "%d", brightness);
            strcat(sendBuff,pwm_data);
            strcat(sendBuff,"_");

            write(connfd,sendBuff,strlen(sendBuff));////
        ////////////////// 모든 디바이스 상태 확인
        }else if(!strcmp(receiveBuff,"th")){//온습도 요청시 읽은 후 준다.
            check_dht11_dat();
            strcpy(th,"th");
            strcat(th,temper);
            strcat(th,"_");
            strcat(th,hum);
            write(connfd,th,strlen(th));//th25.5_55.0
        }
        else if(!strncmp(receiveBuff,"L",1)){                   // 조명밟기 값 받음
            char *sliced_data =strtok(receiveBuff,"L");
            //문자(열) "L"을 기준으로 자름
            strcpy(pwm_data,sliced_data);
            brightness = atoi(pwm_data);
            Set_PWM(brightness);

            printf("보낼 메세지 : %s\n",pwm_data);
            write(connfd, pwm_data, strlen(pwm_data));
        }else if(!strcmp(receiveBuff,"REQL")){
            if(pwm_data[0]==0) // pwm_data가 비어있다면
                strcpy(pwm_data,"0");
            sprintf(pwm_data,"%d",brightness);// 저장된 정수형 밝기값을 char배열형에 저장
            printf("보낼 메세지 : %s\n",pwm_data);

            write(connfd,pwm_data,strlen(pwm_data));
        }
        
        /////////////////////////////////////블루투스 윈도우
        else if(!strcmp(receiveBuff,"WINDOW.C") || !strcmp(receiveBuff,"창문 닫아")){
            Bluetooth_Ctrl("1");                            // 창문 닫아
        }else if(!strcmp(receiveBuff,"WINDOW.O")|| !strcmp(receiveBuff,"창문 열어")){
            Bluetooth_Ctrl("2");                            // 창문 열어
        }else if(!strcmp(receiveBuff,"CURTAIN.C")|| !strcmp(receiveBuff,"커튼 쳐")){
            Bluetooth_Ctrl("3");                            // 커튼 쳐
        }else if(!strcmp(receiveBuff,"CURTAIN.O")|| !strcmp(receiveBuff,"커튼 걷어")){
            Bluetooth_Ctrl("4");                            // 커튼 걷어
        }
        /////////////////////////////////////블루투스 윈도우
        
        else if(!strcmp(receiveBuff,"e")){              // 통신 확인용
            printf("(telnet Debugging) TCP/IP socket OK!\n");
        }
        close(connfd);
    }
    close(listenfd);
    return 0;
}