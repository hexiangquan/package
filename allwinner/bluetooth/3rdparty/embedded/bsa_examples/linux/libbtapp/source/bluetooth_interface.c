/*****************************************************************************
**
**  Name:           bluetooth_interface.c
**
**  Description:    Bluetooth Manager application
**
**  Copyright (c) 2010-2014, Broadcom Corp., All Rights Reserved.
**  Broadcom Bluetooth Core. Proprietary and confidential.
**
*****************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>

#include "bsa_api.h"

#include "app_xml_utils.h"

#include "app_disc.h"
#include "app_utils.h"
#include "app_dm.h"

#include "app_services.h"
#include "app_mgt.h"

#include "app_manager.h"
#include "app_avk.h"
#include "app_hs.h"
#include "bluetooth_interface.h"

#define MAX_PATH_LEN  256

/*
 * Extern variables
 */
extern tAPP_MGR_CB app_mgr_cb;
static tBSA_SEC_IO_CAP g_sp_caps = 0;
extern tAPP_XML_CONFIG         app_xml_config;
tAPP_DISCOVERY_CB app_discovery_cb;
extern tAPP_AVK_CB app_avk_cb;
char bta_conf_path[MAX_PATH_LEN] = {0};

/*static variables */
static BD_ADDR     cur_connected_dev;        /* BdAddr of connected device */
static BD_ADDR     last_connected_dev;       /* store connected dev last reboot */
static void *p_cbt = NULL;
static int discoverable;
static int connectable;

//avk status
static int avk_music_playing = 0;

//discovery
static int  disc_flag = 0;
static int  dev_nums = 0;
static char dev_info[4096] = {0};

//tAvkCallback
static void app_disc_callback(tBSA_DISC_EVT event, tBSA_DISC_MSG *p_data);
static void app_avk_callback(tBSA_AVK_EVT event, tBSA_AVK_MSG *p_data);
static void app_hs_callback(tBSA_HS_EVT event, tBSA_HS_MSG *p_data);

static int store_disc_results(char *buf, int *buf_len)
{
    int num = 0, index = 0, bd_addr = 0;
	  char *pw = NULL, snum[10];
	  int  len = 0;
	  
	  pw = buf;
	  len = 0;
	  for (index = 0; index < APP_DISC_NB_DEVICES; index++)
    {
        if (app_discovery_cb.devs[index].in_use != FALSE)
        {
        	  num++;
        	  len=len+4;
        	  
        	  sprintf(pw, "Dev:%d", num);
        	  pw=pw+4;
        	  if(num<10){
        	      len = len+1;
        	      pw=pw+1;  
        	  }else if(num<100){
        	      len=len+2;
        	      pw=pw+2;
        	  }else{
        	      goto end;
        	  }
        	  
        	  sprintf(pw, "\tBdaddr:%02x:%02x:%02x:%02x:%02x:%02x",
                    app_discovery_cb.devs[index].device.bd_addr[0],
                    app_discovery_cb.devs[index].device.bd_addr[1],
                    app_discovery_cb.devs[index].device.bd_addr[2],
                    app_discovery_cb.devs[index].device.bd_addr[3],
                    app_discovery_cb.devs[index].device.bd_addr[4],
                    app_discovery_cb.devs[index].device.bd_addr[5]);
            len = len+25;
            pw=pw+25;

            sprintf(pw, "\tName:%s", app_discovery_cb.devs[index].device.name);
            len=len+6;
            len=len+strlen(app_discovery_cb.devs[index].device.name);
            pw=pw+6;
            pw=pw+strlen(app_discovery_cb.devs[index].device.name);
            
            *pw='\n';
            len=len+1;
            pw=pw+1;
        }
    }

end:
	  *pw='\0';
	  *buf_len = len;    
    if(num > 0){
        *(pw-1)='\0';
        *buf_len = (len-1);
    }
    
    return 	num;    	
}

static void app_disc_callback(tBSA_DISC_EVT event, tBSA_DISC_MSG *p_data)
{
    int len = 4096;
	  
    switch(event)
    {
    	case BSA_DISC_CMPL_EVT: /* Discovery complete. */
        dev_nums = store_disc_results(dev_info, &len);
        //bt_event_transact(p_sbt, APP_MGR_DISC_RESULTS, buf, &len);
        break;
    }	
}

static void app_avk_callback(tBSA_AVK_EVT event, tBSA_AVK_MSG *p_data)
{
    	
	  switch(event)
	  {
	  	  case BSA_AVK_OPEN_EVT:
	  	  {
	  	  	  /* open status must be success */
	  	  	  if (p_data->open.status == BSA_SUCCESS){
	  	  	      APP_DEBUG0("avk connected!\n");
	  	  	      bt_event_transact(p_cbt, APP_AVK_CONNECTED_EVT, NULL, NULL);
	  	  	      bdcpy(cur_connected_dev, p_data->open.bd_addr);
	  	  	      store_connected_dev(cur_connected_dev);
                              
	  	  	  }
	  	  	    
	  	      break;	
	  	  }
	  	  case BSA_AVK_CLOSE_EVT:
	  	  {
	  	      APP_DEBUG0("avk disconnected!\n");
            bt_event_transact(p_cbt, APP_AVK_DISCONNECTED_EVT, NULL, NULL);
            memset(cur_connected_dev, 0, sizeof(cur_connected_dev));
	  	      break;	
	  	  }
	      case BSA_AVK_START_EVT:
	      {
	      	  if(p_data->start.streaming == TRUE)
	      	  {
                APP_DEBUG0("BT is playing music!\n");
                bt_event_transact(p_cbt, APP_AVK_START_EVT, NULL, NULL);
                avk_music_playing = 1;
	      	  }
	      	  break;
	      }
	      case BSA_AVK_STOP_EVT:
	      {
	      	  APP_DEBUG0("BT is stop music!\n");
	      	  bt_event_transact(p_cbt, APP_AVK_STOP_EVT, NULL, NULL);
                  avk_music_playing = 0;
	      	  break;
	      }
	      
	      default:
	          ;      		
	  }
}

static void app_hs_callback(tBSA_HS_EVT event, tBSA_HS_MSG *p_data)
{
    UINT16 handle = 0;
    tBSA_HS_CONN_CB *p_conn;
    
    /* retrieve the handle of the connection for which this event */
    handle = p_data->hdr.handle;

    /* retrieve the connection for this handle */
    p_conn = app_hs_get_conn_by_handle(handle);
    	
    switch(event)
    {
        case BSA_HS_CONN_EVT:
        {
            APP_DEBUG0("hs connected!\n");
            
            /* check if this conneciton is already opened */
            if (p_conn->connection_active)
            {
                printf("BSA_HS_CONN_EVT: connection already opened for handle %d\n", handle);
                break;
            }
            
            bt_event_transact(p_cbt, APP_HS_CONNECTED_EVT, NULL, NULL);	
            break;	
        }
        case BSA_HS_CLOSE_EVT:
        {
        	  APP_DEBUG0("hs disconnected!\n");
        	  bt_event_transact(p_cbt, APP_AVK_DISCONNECTED_EVT, NULL, NULL);
        	  break;
        }	
        case BSA_HS_AUDIO_OPEN_EVT:
        {
            APP_DEBUG0("hs audio open!\n");
            break;	
        } 			
    }   	
}

/*******************************************************************************
 **
 ** Function         app_mgr_mgt_callback
 **
 ** Description      This callback function is called in case of server
 **                  disconnection (e.g. server crashes)
 **
 ** Parameters
 **
 ** Returns          void
 **
 *******************************************************************************/
static BOOLEAN app_mgr_mgt_callback(tBSA_MGT_EVT event, tBSA_MGT_MSG *p_data)
{
    switch(event)
    {
    case BSA_MGT_STATUS_EVT:
        APP_DEBUG0("BSA_MGT_STATUS_EVT");
        if (p_data->status.enable)
        {
            APP_DEBUG0("Bluetooth restarted => re-initialize the application");
            app_mgr_config();
        }
        break;

    case BSA_MGT_DISCONNECT_EVT:
        APP_DEBUG1("BSA_MGT_DISCONNECT_EVT reason:%d", p_data->disconnect.reason);
        exit(-1);
        break;

    default:
        break;
    }
    return FALSE;
}

int bluetooth_start(void *p, char *p_conf)
{
    int mode;

    p_cbt = p;
   
    if(p_conf && p_conf[0] != '\0')
    {
    	  printf("Bt conf file %s\n", p_conf); 
        strncpy(bta_conf_path, p_conf, MAX_PATH_LEN-1);    	
    }
     	  
    /* Init App manager */
    app_mgt_init();
    if (app_mgt_open(NULL, app_mgr_mgt_callback) < 0)
    {
        APP_ERROR0("Unable to connect to server");
        return -1;
    }

    /* Init XML state machine */
    app_xml_init();

    if (app_mgr_config())
    {
        APP_ERROR0("Couldn't configure successfully, exiting");
        return -1;
    }
    g_sp_caps = app_xml_config.io_cap;

    /* Display FW versions */
    app_mgr_read_version();

    /* Get the current Stack mode */
    mode = app_dm_get_dual_stack_mode();
    if (mode < 0)
    {
        APP_ERROR0("app_dm_get_dual_stack_mode failed");
        return -1;
    }
    else
    {
        /* Save the current DualStack mode */
        app_mgr_cb.dual_stack_mode = mode;
        APP_INFO1("Current DualStack mode:%s", app_mgr_get_dual_stack_mode_desc());
    }
    
    discoverable = 1;
    connectable = 1;
    
    return 0;
}

void start_app_avk()
{
    /* Init avk Application */
    app_avk_init(app_avk_callback);
    //auto register 
    app_avk_register();
}

void start_app_hs()
{
    /* Init Headset Application */
    //app_hs_init();
    /* Start Headset service*/
    //app_hs_start(app_hs_callback);
}

void s_set_bt_name(const char *name)
{
    app_mgr_set_bd_name(name);	
}


void s_set_discoverable(int enable)
{
    if(enable){
        discoverable=1;
    }else{
        discoverable=0;
    }
    app_dm_set_visibility(discoverable, connectable);	
}

void s_set_connectable(int enable)
{
    if(enable){
        connectable=1;
    }else{
        connectable=0;
    }
    app_dm_set_visibility(discoverable, connectable);	
}

void s_start_discovery(int time)
{
    disc_flag = 0;
    dev_nums = 0;
    app_disc_start_regular(app_disc_callback);
}

int s_get_disc_results(char *disc_results, int *len)
{
    int times = 0;

    while((disc_flag == 0) && (times < 300)){
        usleep(100*1000);
        times++;
    }

    if(disc_flag == 0 || dev_nums == 0){
        disc_results[0] = '\0';
        *len = 0;
	return 0;
    }else{
	strncpy(disc_results, dev_info, *len);
	*len = strlen(dev_info);
	disc_results[*len] = '\0';
	return dev_nums;
    }
}

void s_set_volume(int volume)
{
    return ;
}

void s_set_volume_up()
{
    tAPP_AVK_CONNECTION *connection = NULL;
    
    connection = app_avk_find_connection_by_bd_addr(cur_connected_dev);
    if(connection)
    {
        printf("set_volume_up app_avk volume %d\n", app_avk_cb.volume);    	
        app_avk_cb.volume += 8;
        if(app_avk_cb.volume > 127){
            app_avk_cb.volume = 127;
        }
        app_avk_reg_notfn_rsp(app_avk_cb.volume,
            connection->rc_handle,
            connection->volChangeLabel,
            AVRC_EVT_VOLUME_CHANGE,
            BTA_AV_RSP_CHANGED);
        printf("volume up change locally at TG\n");    	
    }
    else
    {
        printf("Connection is NULL when set volume up\n");	
    }	
}

void s_set_volume_down()
{
    tAPP_AVK_CONNECTION *connection = NULL;
    
    connection = app_avk_find_connection_by_bd_addr(cur_connected_dev);
    if(connection)
    {
    	  printf("set_volume_down app_avk volume %d\n", app_avk_cb.volume);
        if(app_avk_cb.volume < 8 ){
            app_avk_cb.volume = 0;
        } else {
            app_avk_cb.volume -= 8;
        }
        
        app_avk_reg_notfn_rsp(app_avk_cb.volume,
            connection->rc_handle,
            connection->volChangeLabel,
            AVRC_EVT_VOLUME_CHANGE,
            BTA_AV_RSP_CHANGED);
        printf("volume down change locally at TG\n");    	
    }
    else
    {
        printf("Connection is NULL when set volume down\n");	
    }	
}

void s_connect_auto()
{
    printf("s connect auto\n");
    memset(last_connected_dev, 0, sizeof(last_connected_dev));
    read_connected_dev(last_connected_dev);
    app_avk_auto_connect(last_connected_dev);    	
}

void s_disconnect()
{
    int i = 0;

    printf("s disconnect, music playing %d\n", avk_music_playing);
    if(avk_music_playing == 1){
        s_avk_pause();
        do{
           usleep(100*1000);
           i++;
        }while((avk_music_playing != 0) && (i<=20));
    }
    
    app_avk_close(cur_connected_dev);
}

void s_avk_play()
{
    tAPP_AVK_CONNECTION *connection = NULL;
 
    printf("a avk play\n");	  

	  connection = app_avk_find_connection_by_bd_addr(cur_connected_dev);
	  if(connection)
	  {
        app_avk_play_start(connection->rc_handle);
    }
    else
    {
    	  printf("Connection is NULL when playing\n");
    }	
}

void s_avk_pause()
{
	  tAPP_AVK_CONNECTION *connection = NULL;

          printf("s avk pause\n");	  

	  connection = app_avk_find_connection_by_bd_addr(cur_connected_dev);
	  if(connection)
	  {
        app_avk_play_pause(connection->rc_handle);
    }
    else
    {
    	  printf("Connection is NULL when pausing\n");
    }
}

void s_avk_stop()
{
    tAPP_AVK_CONNECTION *connection = NULL;

    printf("s avk stop\n");	  
    connection = app_avk_find_connection_by_bd_addr(cur_connected_dev);
    if(connection)
    {
        app_avk_play_stop(connection->rc_handle);
    }
    else
    {
        printf("Connection is NULL when pausing\n");
    }
}

void s_avk_play_previous()
{
	  tAPP_AVK_CONNECTION *connection = NULL;
	  
	  connection = app_avk_find_connection_by_bd_addr(cur_connected_dev);
	  if(connection)
	  {
        app_avk_play_previous_track(connection->rc_handle);
    }
    else
    {
    	  printf("Connection is NULL when playing pre\n");
    }
}

void s_avk_play_next()
{
    tAPP_AVK_CONNECTION *connection = NULL;
	  
	  connection = app_avk_find_connection_by_bd_addr(cur_connected_dev);
	  if(connection)
	  {
        app_avk_play_next_track(connection->rc_handle);
    }
    else
    {
    	  printf("Connection is NULL when playing next\n");
    }	
}

void s_hs_pick_up()
{
    app_hs_answer_call();
}

void s_hs_hung_up()
{
    app_hs_hangup();
}

void stop_app_avk()
{ 
    app_avk_deregister();
    /* Terminate the avk profile */
    app_avk_end();
}

void stop_app_hs()
{
    /* Stop Headset service*/
    //app_hs_stop();
}

void bluetooth_stop()
{
    app_mgt_close();
    discoverable = 0;
    connectable = 0;
}
