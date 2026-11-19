#include <Wire.h> 
#include <LiquidCrystal_I2C.h>
#include <DHT.h>

#define   dht11Pin      4       //定义温湿度传感器引脚
#define   ledPin       12      //定义LED灯模块引脚
#define   buzzerPin    15      //定义有源蜂鸣器引脚
#define   INAPin       18      //定义电机驱动模块引脚
#define   INBPin       19      //定义电机驱动模块引脚
#define   soilPin      33      //定义土壤湿度模块引脚
#define   waterPin     34      //定义水位传感器引脚
#define   lightPin     35      //定义光敏模块引脚   

int Light;
float Water,data,Humi,Temp,Soil,i,j;

DHT dht(dht11Pin, DHT11);
LiquidCrystal_I2C lcd(0x27,16,2);  

void setup(){
  Lcd();
  pinMode(lightPin,INPUT);
  pinMode(soilPin ,INPUT);
  pinMode(dht11Pin,INPUT);
  pinMode(waterPin,INPUT);
  pinMode(ledPin,OUTPUT); 
  pinMode(buzzerPin,OUTPUT);  
  pinMode(INAPin,OUTPUT);
  pinMode(INBPin,OUTPUT);
  digitalWrite(ledPin,LOW);
  analogWrite(INAPin,255); 
  analogWrite(INBPin,255);     
  digitalWrite(buzzerPin,LOW);   
  Serial.begin(9600);
}

void loop(){
  dht11();
  light();
  Display();
  soil();
}

void light()
{
   Light =  analogRead(lightPin);     //读取光敏传感器模拟值
   if(Light > 500)                    //当模拟值大于500，即处于光线正常环境时，灯关闭
   {
   digitalWrite(ledPin,LOW);
   }
   else
   {
   digitalWrite(ledPin,HIGH);
   }
}


void soil()
{
    Soil=analogRead(soilPin);
    Water=analogRead(waterPin);
    data=(Water/1600)*4;
    Serial.println(data);
   
    i=Soil/4095;
    j=(1-i)*100;
    if (j < 30){
      if (data > 2){
        analogWrite(INAPin,0);
        analogWrite(INBPin,255);
        digitalWrite(buzzerPin,LOW);
      }
      else{
        analogWrite(INAPin,255);
        analogWrite(INBPin,255);
        digitalWrite(buzzerPin,HIGH);
      }
    }
    else if(j >= 30)
    {
      if (data > 2){
        analogWrite(INAPin,255);
        analogWrite(INBPin,255);
        digitalWrite(buzzerPin,LOW);     
     }
       else {
        analogWrite(INAPin,255);
        analogWrite(INBPin,255);
        digitalWrite(buzzerPin,HIGH); 
      }
     }
}

void dht11()
{
    Temp = dht.readTemperature();
    Humi = dht.readHumidity();
    
}

void Lcd()
{
  lcd.init();                    
  lcd.backlight();
  lcd.setCursor(1,0);
  lcd.print("Automatic watering");
  lcd.setCursor(1,1);
  lcd.print("Let's do it!");
  delay(1000);
  lcd.clear();
}


void Display()
{
  lcd.setCursor(0,0);
  lcd.print("T:");
  lcd.print(Temp);
  lcd.setCursor(8,0);
  lcd.print("H:");
  lcd.print(Humi);
  lcd.setCursor(0,1);
  lcd.print("L:");
  lcd.print(Light);
  lcd.print("   ");
  lcd.setCursor(8,1);
  lcd.print("S:");
  lcd.print(j,1);
  lcd.setCursor(15,1);
  lcd.print("%");
  delay(500);
}
