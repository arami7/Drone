#include <stdlib.h>
#include <string.h>
#include "usart.h"
#include "mti.h"

MTIData mti_data;
MTIState mti_state;
MTI mti;

uint8_t mti_rx_flag;
uint8_t mti_checksum_flag;
uint8_t mti_rx_buff[MTI_DMA_RX_SIZE];
uint8_t mti_packet_buff[MTI_PACKET_SIZE];
int rx_size = 0;

/* mti 초기화 함수 */
void init_mti()
{
  mti_state.new_packet_flag = 1;
  mti_state.packet_rx_flag = 0;
  mti_state.checksum_flag = 0;
  mti_state.count = 0;
}

/* 모든 mti 과정 실행하는 함수 */
void read_mti()
{
  receive_mti_packet();
  if(mti_state.packet_rx_flag)
  {
    check_mti_packet();
    if(mti_state.checksum_flag)
    {
      decode_mti_packet();
    }
  }
}

/* mti로부터 DMA로 패킷 받아오는 함수 */
void receive_mti_packet()
{
    static int prev_ndt = MTI_DMA_RX_SIZE, curr_ndt = 0;                                //이전 ndt, 현재 ndt
    static int new_data_start_idx = 0, next_data_start_idx = 0;                         //현재 새롭게 들어온 데이터의 인덱스, 다음 루틴에 들어올 데이터의 인덱스  
    static int packet_start_idx = 0, packet_end_idx = MTI_DMA_RX_SIZE;        //온전한 패킷의 시작 인덱스, 끝 인덱스
    int check_idx, next_check_idx;                                                            // 버퍼의 내용을 검사할 인덱스와 그 옆의 인덱스                                                                                         
    uint8_t received_data_size = 0;                                                             //수신한 데이터 수
    
    /* 현재 ndt 구하기 */
    curr_ndt = __HAL_DMA_GET_COUNTER(&hdma_usart3_rx);

    /* 이전 ndt와 현재 ndt의 차이에 따른 처리 */
    if(prev_ndt > curr_ndt)
    {
      received_data_size = prev_ndt - curr_ndt;
    }
    else if(prev_ndt < curr_ndt)
    {
      received_data_size = prev_ndt + (MTI_DMA_RX_SIZE - curr_ndt);
    }
    else
    {
      return;
    }

    /* DMA로 수신되는 버퍼의 현재 인덱스(마지막으로 들어온 인덱스 + 1) 구하기 */
    rx_size += received_data_size;
    next_data_start_idx = (new_data_start_idx + received_data_size) % MTI_DMA_RX_SIZE;
    
    /* 현재 들어온 데이터의 시작 인덱스부터 마지막 인덱스까지 검사 */
    check_idx = new_data_start_idx - 1;
    next_check_idx = check_idx + 1;
    if(check_idx == -1) 
    {
      check_idx = MTI_DMA_RX_SIZE - 1;
      next_check_idx = 0;
    }
    /* 현재 들어온 데이터의 마지막 인덱스까지 검사를 마치면 break */
    while(!(next_check_idx == next_data_start_idx))
    {
      /* 스타트 바이트 검사 */
      if((mti_state.new_packet_flag) && ((mti_rx_buff[check_idx] == 0xfa) && (mti_rx_buff[next_check_idx] == 0xff)))
      {
        mti_state.new_packet_flag = 0;
        packet_start_idx = check_idx;
        packet_end_idx = (packet_start_idx + (MTI_PACKET_SIZE - 1)) % MTI_DMA_RX_SIZE;
      }
      
      /* 링버퍼로 인한 인덱스 변화 처리 */
      check_idx = (check_idx+1) % MTI_DMA_RX_SIZE;
      next_check_idx = (check_idx+1) % MTI_DMA_RX_SIZE;
      
      /* 체크섬 바이트가 들어왔다면*/
      if(check_idx == packet_end_idx)
      { 
        /* 패킷의 시작 , 끝 인덱스의 차이에 따른 처리
            온전한 한 패킷만을 담아놓는 버퍼로 복사 */
        if(packet_start_idx < packet_end_idx)
        {               
          memcpy(mti_packet_buff, (mti_rx_buff + packet_start_idx), ((packet_end_idx - packet_start_idx) + 1));
        }
        else if(packet_start_idx > packet_end_idx)
        {
          memcpy(mti_packet_buff, (mti_rx_buff + packet_start_idx), (MTI_DMA_RX_SIZE - packet_start_idx));
          memcpy((mti_packet_buff + (MTI_DMA_RX_SIZE - packet_start_idx)), mti_rx_buff, (packet_end_idx + 1));
        }
        mti_state.new_packet_flag = 1;
        mti_state.packet_rx_flag = 1;
      }
    }
    
    /* 현재 값을 이전 값으로 세팅 */
    prev_ndt = curr_ndt;
    new_data_start_idx = next_data_start_idx;
}

/* mti 패킷 무결성 체크 함수 */
void check_mti_packet()
{
  int i;
  uint16_t result = 0, checksum = 0;
  
  mti_state.packet_rx_flag = 0;
  mti_state.checksum_flag = 0;
  
  for(i = 1; i < MTI_PACKET_SIZE; i++)
  {
    result += mti_packet_buff[i];
  }
  checksum = result & 0x00ff;
  
  /* 패킷의 무결성이 결정되면 flag SET */
  if(checksum == 0x0000)
  {
    mti_state.checksum_flag = 1;
  }
}

/* mti 패킷 디코딩 함수 */
void decode_mti_packet()
{
  /* euler angles 디코딩 */
  mti_data.buff[0] = mti_packet_buff[10];
  mti_data.buff[1] = mti_packet_buff[9];
  mti_data.buff[2] = mti_packet_buff[8];
  mti_data.buff[3] = mti_packet_buff[7];
  mti_data.buff[4] = mti_packet_buff[14];
  mti_data.buff[5] = mti_packet_buff[13];
  mti_data.buff[6] = mti_packet_buff[12];
  mti_data.buff[7] = mti_packet_buff[11];
  mti_data.buff[8] = mti_packet_buff[18];
  mti_data.buff[9] = mti_packet_buff[17];
  mti_data.buff[10] = mti_packet_buff[16];
  mti_data.buff[11] = mti_packet_buff[15];
  
  /* acceleration  디코딩 */
  mti_data.buff[12] = mti_packet_buff[25];
  mti_data.buff[13] = mti_packet_buff[24];
  mti_data.buff[14] = mti_packet_buff[23];
  mti_data.buff[15] = mti_packet_buff[22];
  mti_data.buff[16] = mti_packet_buff[29];
  mti_data.buff[17] = mti_packet_buff[28];
  mti_data.buff[18] = mti_packet_buff[27];
  mti_data.buff[19] = mti_packet_buff[26];
  mti_data.buff[20] = mti_packet_buff[33];
  mti_data.buff[21] = mti_packet_buff[32];
  mti_data.buff[22] = mti_packet_buff[31];
  mti_data.buff[23] = mti_packet_buff[30];

  /* delta_v 디코딩 */
  mti_data.buff[24] = mti_packet_buff[40];
  mti_data.buff[25] = mti_packet_buff[39];
  mti_data.buff[26] = mti_packet_buff[38];
  mti_data.buff[27] = mti_packet_buff[37];
  mti_data.buff[28] = mti_packet_buff[44];
  mti_data.buff[29] = mti_packet_buff[43];
  mti_data.buff[30] = mti_packet_buff[42];
  mti_data.buff[31] = mti_packet_buff[41];
  mti_data.buff[32] = mti_packet_buff[48];
  mti_data.buff[33] = mti_packet_buff[47];
  mti_data.buff[34] = mti_packet_buff[46];
  mti_data.buff[35] = mti_packet_buff[45];
  
  /* gyro 디코딩 */ 
  mti_data.buff[36] = mti_packet_buff[55];
  mti_data.buff[37] = mti_packet_buff[54]; 
  mti_data.buff[38] = mti_packet_buff[53]; 
  mti_data.buff[39] = mti_packet_buff[52]; 
  mti_data.buff[40] = mti_packet_buff[59]; 
  mti_data.buff[41] = mti_packet_buff[58]; 
  mti_data.buff[42] = mti_packet_buff[57]; 
  mti_data.buff[43] = mti_packet_buff[56]; 
  mti_data.buff[44] = mti_packet_buff[63]; 
  mti_data.buff[45] = mti_packet_buff[62]; 
  mti_data.buff[46] = mti_packet_buff[61]; 
  mti_data.buff[47] = mti_packet_buff[60]; 
  
  /* delta_q 디코딩 */
  mti_data.buff[48] = mti_packet_buff[70];
  mti_data.buff[49] = mti_packet_buff[69];
  mti_data.buff[50] = mti_packet_buff[68];
  mti_data.buff[51] = mti_packet_buff[67];
  mti_data.buff[52] = mti_packet_buff[74];
  mti_data.buff[53] = mti_packet_buff[73];
  mti_data.buff[54] = mti_packet_buff[72];
  mti_data.buff[55] = mti_packet_buff[71];
  mti_data.buff[56] = mti_packet_buff[78];
  mti_data.buff[57] = mti_packet_buff[77];
  mti_data.buff[58] = mti_packet_buff[76];
  mti_data.buff[59] = mti_packet_buff[75];
  mti_data.buff[60] = mti_packet_buff[82];
  mti_data.buff[61] = mti_packet_buff[81];
  mti_data.buff[62] = mti_packet_buff[80];
  mti_data.buff[63] = mti_packet_buff[79];
  
  /* magnetic_field 디코딩 */
  mti_data.buff[64] = mti_packet_buff[89];
  mti_data.buff[65] = mti_packet_buff[88];
  mti_data.buff[66] = mti_packet_buff[87];
  mti_data.buff[67] = mti_packet_buff[86];
  mti_data.buff[68] = mti_packet_buff[93];
  mti_data.buff[69] = mti_packet_buff[92];
  mti_data.buff[70] = mti_packet_buff[91];
  mti_data.buff[71] = mti_packet_buff[90];
  mti_data.buff[72] = mti_packet_buff[97];
  mti_data.buff[73] = mti_packet_buff[96];
  mti_data.buff[74] = mti_packet_buff[95];
  mti_data.buff[75] = mti_packet_buff[94];
  
  mti.euler[0] = mti_data.value[0];
  mti.euler[1] = mti_data.value[1];
  mti.euler[2] = mti_data.value[2];
  
  mti.acc[0] = mti_data.value[3];
  mti.acc[1] = mti_data.value[4];
  mti.acc[2] = mti_data.value[5];
  
  mti.delta_v[0] = mti_data.value[6];
  mti.delta_v[1] = mti_data.value[7];
  mti.delta_v[2] = mti_data.value[8];
   
  mti.pqr[0] = mti_data.value[9];
  mti.pqr[1] = mti_data.value[10];
  mti.pqr[2] = mti_data.value[11];
  
  mti.delta_q[0] = mti_data.value[12];
  mti.delta_q[1] = mti_data.value[13];
  mti.delta_q[2] = mti_data.value[14];
  mti.delta_q[3] = mti_data.value[15];
  
  mti.mag[0] = mti_data.value[16];
  mti.mag[1] = mti_data.value[17];
  mti.mag[2] = mti_data.value[18];
  
  mti_state.count++;
  //printf("%4f %4f %4f\r\n", mti.euler[0], mti.euler[1], mti.euler[2]);
  //printf("%4f %4f %4f\r\n", mti.acc[0], mti.acc[1], mti.acc[2]);
  //printf("%4f %4f %4f\r\n", mti.delta_v[0], mti.delta_v[1], mti.delta_v[2]);
  //printf("%4f %4f %4f\r\n", mti.pqr[0], mti.pqr[1], mti.pqr[2]);
  //printf("%4f %4f %4f %4f\r\n", mti.delta_q[0], mti.delta_q[1], mti.delta_q[2], mti.delta_q[3]);
  //printf("%4f %4f %4f\r\n", mti.mag[0], mti.mag[1], mti.mag[2]);
  //printf("\r\n");
}