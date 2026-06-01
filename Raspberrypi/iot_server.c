/* 서울기술교육센터 AIoT - SmartFarm Server */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
#include <errno.h>
#include <mysql/mysql.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>
#include <math.h>

#define BUF_SIZE        200
#define MAX_CLNT        34
#define ID_SIZE         20
#define ARR_CNT         10
#define HTTP_PORT       8080

#define STM32_BT_ADDR   "98:DA:60:0D:AF:75"
#define STM32_BT_CH     1

#define DB_HOST         "127.0.0.1"
#define DB_USER         "iot"
#define DB_PASS         "pwiot"
#define DB_NAME         "iotdb"

/* ── 계절 온도 기준 ── */
#define TEMP_WINTER     10.0f   /* 이하 → 겨울 */
#define TEMP_SUMMER     25.0f   /* 이상 → 여름 */

/* 여름: 이 온도 이상이면 LED 밝기 낮춤 */
#define TEMP_SUMMER_DIM 35.0f
/* 겨울: 이 온도 이하이면 LED 밝기 올림 */
#define TEMP_WINTER_BRT  5.0f

typedef struct {
    char  fd;
    char *from;
    char *to;
    char *msg;
    int   len;
} MSG_INFO;

typedef struct {
    int  index;
    int  fd;
    char ip[20];
    char id[ID_SIZE];
    char pw[ID_SIZE];
} CLIENT_INFO;

/* ── 전역 ── */
int              clnt_cnt  = 0;
pthread_mutex_t  mutx;
pthread_mutex_t  bt_mutx;
pthread_mutex_t  db_mutx;
int              g_bt_fd   = -1;
MYSQL           *g_db      = NULL;

/* 현재 계절 상태 (웹 API용) */
static char g_season[16]   = "spring";
static float g_last_temp   = 0.0f;
static float g_last_humi   = 0.0f;
static int   g_last_flame  = 0;

void *clnt_connection(void *arg);
void *http_server_thread(void *arg);
void *bt_recv_thread(void *arg);
void  send_msg(MSG_INFO *msg_info, CLIENT_INFO *first);
void  error_handling(char *msg);
void  log_file(char *msgstr);
void  getlocaltime(char *buf);
int   db_init(void);
void  db_insert_sensor(const char *id, int illu, float temp,
                        float humi, int flame);
void  db_set_actuator(const char *name, const char *value);
int   db_get_actuator(const char *name, char *out, size_t len);
int   bt_connect_stm32(void);
void  bt_send(const char *cmd);

/* ════════════════════════════════════
 *  계절 판단 + STM32 LED 제어
 * ════════════════════════════════════ */
void process_season(float temp)
{
    char cmd[64];

     if (temp <= TEMP_WINTER) {
        strcpy(g_season, "winter");
        bt_send("CMD:LED:B\n");

    } else if (temp >= TEMP_SUMMER) {
        strcpy(g_season, "summer");
        bt_send("CMD:LED:R\n");

    } else {
        strcpy(g_season, "spring");
        bt_send("CMD:LED:G\n");
    }
}

/* ════════════════════════════════════
 *  MariaDB
 * ════════════════════════════════════ */
int db_init(void)
{
    g_db = mysql_init(NULL);
    if (!g_db) return -1;
    my_bool rec = 1;
    mysql_options(g_db, MYSQL_OPT_RECONNECT, &rec);
    if (!mysql_real_connect(g_db, DB_HOST, DB_USER, DB_PASS,
                            DB_NAME, 0, NULL, 0)) {
        fprintf(stderr, "[DB] %s\n", mysql_error(g_db));
        return -1;
    }
    printf("[DB] MariaDB 연결 성공\n");
    return 0;
}

void db_insert_sensor(const char *id, int illu,
                      float temp, float humi, int flame)
{
    char sql[300];
    snprintf(sql, sizeof(sql),
        "INSERT INTO sensor(name,date,time,illu,temp,humi,flame) "
        "VALUES('%s',now(),now(),%d,%.2f,%.2f,%d)",
        id, illu, temp, humi, flame);
    pthread_mutex_lock(&db_mutx);
    if (mysql_query(g_db, sql))
        fprintf(stderr, "[DB] insert error: %s\n", mysql_error(g_db));
    pthread_mutex_unlock(&db_mutx);
}

void db_set_actuator(const char *name, const char *value)
{
    char sql[200];
    snprintf(sql, sizeof(sql),
        "UPDATE device SET value='%s',date=now(),time=now() "
        "WHERE name='%s'", value, name);
    pthread_mutex_lock(&db_mutx);
    if (mysql_query(g_db, sql))
        fprintf(stderr, "[DB] update error: %s\n", mysql_error(g_db));
    pthread_mutex_unlock(&db_mutx);
}

int db_get_actuator(const char *name, char *out, size_t len)
{
    char sql[200];
    snprintf(sql, sizeof(sql),
        "SELECT value FROM device WHERE name='%s'", name);
    pthread_mutex_lock(&db_mutx);
    int ret = -1;
    if (!mysql_query(g_db, sql)) {
        MYSQL_RES *res = mysql_store_result(g_db);
        if (res) {
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row) { strncpy(out, row[0], len-1); ret = 0; }
            mysql_free_result(res);
        }
    }
    pthread_mutex_unlock(&db_mutx);
    return ret;
}

/* ════════════════════════════════════
 *  Bluetooth
 * ════════════════════════════════════ */
int bt_connect_stm32(void)
{
    struct sockaddr_rc addr = {};
    g_bt_fd = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
    if (g_bt_fd < 0) { perror("[BT] socket"); return -1; }
    addr.rc_family  = AF_BLUETOOTH;
    addr.rc_channel = STM32_BT_CH;
    str2ba(STM32_BT_ADDR, &addr.rc_bdaddr);
    if (connect(g_bt_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[BT] connect");
        close(g_bt_fd); g_bt_fd = -1; return -1;
    }
    printf("[BT] STM32 연결 성공 (%s)\n", STM32_BT_ADDR);
    return 0;
}

void bt_send(const char *cmd)
{
    if (g_bt_fd < 0) {
        printf("[BT] STM32 미연결 — 명령 무시: %s\n", cmd);
        return;
    }
    pthread_mutex_lock(&bt_mutx);
    write(g_bt_fd, cmd, strlen(cmd));
    pthread_mutex_unlock(&bt_mutx);
    printf("[BT→STM32] %s\n", cmd);
}

void *bt_recv_thread(void *arg)
{
    (void)arg;
    char buf[BUF_SIZE];
    while (1) {
        if (g_bt_fd < 0) { sleep(3); continue; }
        ssize_t n = read(g_bt_fd, buf, sizeof(buf)-1);
        if (n <= 0) { sleep(2); continue; }
        buf[n] = '\0';
        printf("[BT←STM32] %s\n", buf);
        char sql[300];
        snprintf(sql, sizeof(sql),
            "INSERT INTO actuator_log(raw_msg,created_at) "
            "VALUES('%s',NOW())", buf);
        pthread_mutex_lock(&db_mutx);
        mysql_query(g_db, sql);
        pthread_mutex_unlock(&db_mutx);
    }
    return NULL;
}

/* ════════════════════════════════════
 *  HTTP REST API
 * ════════════════════════════════════ */
static void http_send(int fd, int code, const char *body)
{
    char hdr[512];
    snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n\r\n",
        code, code==200?"OK":"Error", (int)strlen(body));
    write(fd, hdr, strlen(hdr));
    write(fd, body, strlen(body));
}

static void handle_get_sensors(int fd)
{
    pthread_mutex_lock(&db_mutx);
    char json[8192] = "[";
    int first = 1;
    if (!mysql_query(g_db,
        "SELECT name,illu,temp,humi,flame,"
        "UNIX_TIMESTAMP(CONCAT(date,' ',time)) as ts "
        "FROM sensor ORDER BY id DESC LIMIT 20")) {
        MYSQL_RES *res = mysql_store_result(g_db);
        if (res) {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res))) {
                char e[256];
                if (!first) strcat(json, ",");
                snprintf(e, sizeof(e),
                    "{\"id\":\"%s\",\"illu\":%s,\"temp\":%s,"
                    "\"humi\":%s,\"flame\":%s,\"ts\":%s}",
                    row[0],row[1],row[2],row[3],row[4],row[5]);
                strcat(json, e); first = 0;
            }
            mysql_free_result(res);
        }
    }
    pthread_mutex_unlock(&db_mutx);
    strcat(json, "]");
    http_send(fd, 200, json);
}

static void handle_get_actuators(int fd)
{
    pthread_mutex_lock(&db_mutx);
    char json[4096] = "[";
    int first = 1;
    if (!mysql_query(g_db, "SELECT name,value FROM device")) {
        MYSQL_RES *res = mysql_store_result(g_db);
        if (res) {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res))) {
                char e[128];
                if (!first) strcat(json, ",");
                snprintf(e, sizeof(e),
                    "{\"name\":\"%s\",\"value\":\"%s\"}",
                    row[0], row[1]);
                strcat(json, e); first = 0;
            }
            mysql_free_result(res);
        }
    }
    pthread_mutex_unlock(&db_mutx);
    strcat(json, "]");
    http_send(fd, 200, json);
}

/* GET /api/status — 계절+최신센서 한번에 */
static void handle_get_status(int fd)
{
    char json[256];
    snprintf(json, sizeof(json),
        "{\"season\":\"%s\",\"temp\":%.2f,\"humi\":%.2f,\"flame\":%d}",
        g_season, g_last_temp, g_last_humi, g_last_flame);
    http_send(fd, 200, json);
}

static void handle_post_control(int fd, const char *body)
{
    char target[32] = "", value[32] = "";
    const char *p;
    p = strstr(body, "target=");
    if (p) sscanf(p+7, "%31[^&\r\n]", target);
    p = strstr(body, "value=");
    if (p) sscanf(p+6, "%31[^&\r\n]", value);

    if (!target[0] || !value[0]) {
        http_send(fd, 400, "{\"error\":\"invalid params\"}");
        return;
    }

    char cmd[64];
    snprintf(cmd, sizeof(cmd), "CMD:%s:%s\n", target, value);
    bt_send(cmd);
    db_set_actuator(target, value);
    http_send(fd, 200, "{\"ok\":true}");
}

static void *http_client_thread(void *arg)
{
    int fd = *(int *)arg; free(arg);
    char buf[2048] = "";
    ssize_t n = recv(fd, buf, sizeof(buf)-1, 0);
    if (n <= 0) { close(fd); return NULL; }
    buf[n] = '\0';

    char method[8], path[128];
    sscanf(buf, "%7s %127s", method, path);

    const char *body = strstr(buf, "\r\n\r\n");
    body = body ? body+4 : "";

    if (strcmp(method,"GET")==0 && strcmp(path,"/api/sensors")==0)
        handle_get_sensors(fd);
    else if (strcmp(method,"GET")==0 && strcmp(path,"/api/actuators")==0)
        handle_get_actuators(fd);
    else if (strcmp(method,"GET")==0 && strcmp(path,"/api/status")==0)
        handle_get_status(fd);
    else if (strcmp(method,"POST")==0 && strcmp(path,"/api/control")==0)
        handle_post_control(fd, body);
    else
        http_send(fd, 404, "{\"error\":\"not found\"}");

    close(fd);
    return NULL;
}

void *http_server_thread(void *arg)
{
    (void)arg;
    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(HTTP_PORT);
    bind(sfd, (struct sockaddr *)&addr, sizeof(addr));
    listen(sfd, 10);
    printf("[HTTP] 포트 %d 열림\n", HTTP_PORT);

    while (1) {
        int cfd = accept(sfd, NULL, NULL);
        if (cfd < 0) continue;
        int *fdp = malloc(sizeof(int)); *fdp = cfd;
        pthread_t tid;
        pthread_create(&tid, NULL, http_client_thread, fdp);
        pthread_detach(tid);
    }
    return NULL;
}

/* ════════════════════════════════════
 *  클라이언트 연결 처리
 * ════════════════════════════════════ */
void *clnt_connection(void *arg)
{
    CLIENT_INFO *client_info = (CLIENT_INFO *)arg;
    int   str_len = 0;
    int   index   = client_info->index;
    char  msg[BUF_SIZE];
    char  to_msg[MAX_CLNT*ID_SIZE+1];
    int   i = 0;
    char *pToken;
    char *pArray[ARR_CNT] = {0};
    char  strBuff[BUF_SIZE*2] = {0};

    MSG_INFO    msg_info;
    CLIENT_INFO *first = (CLIENT_INFO *)
        ((void *)client_info - sizeof(CLIENT_INFO)*index);

    while (1) {
        memset(msg, 0, sizeof(msg));
        str_len = read(client_info->fd, msg, sizeof(msg)-1);
        if (str_len <= 0) break;
        msg[str_len] = '\0';

        /* SENSOR 패킷 처리 */
        if (strstr(msg, "SENSOR@") != NULL) {
            char raw[BUF_SIZE];
            strncpy(raw, msg, sizeof(raw)-1);
            int   illu  = 0;
            float temp  = 0, humi = 0;
            int   flame = 0;

            char *tok = strtok(raw, "@");
            tok = strtok(NULL, "@"); if(tok) illu  = atoi(tok);
            tok = strtok(NULL, "@"); if(tok) temp  = atof(tok);
            tok = strtok(NULL, "@"); if(tok) humi  = atof(tok);
            tok = strtok(NULL, "@"); if(tok) flame = atoi(tok);

            db_insert_sensor(client_info->id, illu, temp, humi, flame);
            printf("[SENSOR] %s illu=%d temp=%.2f humi=%.2f flame=%d\n",
                   client_info->id, illu, temp, humi, flame);
            
            /* CDS 값에 따라 STM32 LED 밝기 조절 */
            if (illu >= 650) {
                bt_send("CMD:LIGHT:HIGH\n");
            } else if (illu >= 600) {
                bt_send("CMD:LIGHT:MID\n");
            } else {
                bt_send("CMD:LIGHT:LOW\n");
            }
                
            /* 전역 최신값 업데이트 */
            g_last_temp  = temp;
            g_last_humi  = humi;
            g_last_flame = flame;

          /* 계절 판단 → 매번 호출 말고 온도 변화 있을 때만 */
static float last_season_temp = -999.0f;
if (fabsf(temp - last_season_temp) >= 1.0f) {
    //process_season(temp);
    last_season_temp = temp;
}
/* 화재 감지 → 부저 ON + 워터펌프 ON */
static int last_flame = -1;
static int fire_active = 0;
static time_t fire_off_time = 0;

/* 화재 OFF 타이머 체크 (매 센서 수신마다) */
if (fire_active && fire_off_time > 0 && time(NULL) >= fire_off_time) {
    bt_send("CMD:BUZZER:OFF\n");
    usleep(100000);
    bt_send("CMD:LED:G\n");
    usleep(100000);
    bt_send("CMD:PUMP:OFF\n");
    db_set_actuator("BUZZER", "OFF");
    db_set_actuator("PUMP",   "OFF");
    printf("[ALERT] 부저+펌프 OFF\n");
    fire_active   = 0;
    fire_off_time = 0;
    last_flame    = 0;
}

if (flame != last_flame) {
    if (flame) {
        bt_send("CMD:BUZZER:ON\n");
        usleep(100000);
        bt_send("CMD:LED:R\n");
        usleep(100000);
        bt_send("CMD:PUMP:ON\n");
        db_set_actuator("BUZZER", "ON");
        db_set_actuator("PUMP",   "ON");
        printf("[ALERT] 화재 감지! 부저+펌프 ON\n");
        fire_active   = 1;
        fire_off_time = time(NULL) + 5;
    }
    last_flame = flame;
}
continue;
        }

        /* 기존 프로토콜 */
        pToken = strtok(msg, "[:]");
        i = 0;
        while (pToken != NULL) {
            pArray[i] = pToken;
            if (i++ >= ARR_CNT) break;
            pToken = strtok(NULL, "[:]");
        }

        if (i >= 3 && !strcmp(pArray[1], "STM32")) {
            char cmd[64];
            snprintf(cmd, sizeof(cmd), "CMD:%s\n", pArray[2]);
            bt_send(cmd);
            continue;
        }

        msg_info.fd   = client_info->fd;
        msg_info.from = client_info->id;
        msg_info.to   = pArray[0];
        snprintf(to_msg, sizeof(to_msg), "[%s]%s",
                 msg_info.from, pArray[1] ? pArray[1] : "");
        msg_info.msg = to_msg;
        msg_info.len = strlen(to_msg);

        snprintf(strBuff, sizeof(strBuff),
                 "msg: [%s→%s] %s\n",
                 msg_info.from, msg_info.to,
                 pArray[1] ? pArray[1] : "");
        log_file(strBuff);
        send_msg(&msg_info, first);
    }

    close(client_info->fd);
    snprintf(strBuff, sizeof(strBuff),
             "Disconnect ID:%s (ip:%s,fd:%d,cnt:%d)\n",
             client_info->id, client_info->ip,
             client_info->fd, clnt_cnt-1);
    log_file(strBuff);

    pthread_mutex_lock(&mutx);
    clnt_cnt--;
    client_info->fd = -1;
    pthread_mutex_unlock(&mutx);
    return NULL;
}

void send_msg(MSG_INFO *msg_info, CLIENT_INFO *first)
{
    int i;
    if (!strcmp(msg_info->to, "ALLMSG")) {
        for (i = 0; i < MAX_CLNT; i++)
            if ((first+i)->fd != -1)
                write((first+i)->fd, msg_info->msg, msg_info->len);
    } else if (!strcmp(msg_info->to, "IDLIST")) {
        char idlist[ID_SIZE*MAX_CLNT+1];
        strcpy(idlist, msg_info->msg);
        for (i = 0; i < MAX_CLNT; i++)
            if ((first+i)->fd != -1) {
                strcat(idlist, (first+i)->id);
                strcat(idlist, " ");
            }
        strcat(idlist, "\n");
        write(msg_info->fd, idlist, strlen(idlist));
    } else if (!strcmp(msg_info->to, "GETTIME")) {
        sleep(1);
        getlocaltime(msg_info->msg);
        write(msg_info->fd, msg_info->msg, strlen(msg_info->msg));
    } else {
        for (i = 0; i < MAX_CLNT; i++)
            if ((first+i)->fd != -1 &&
                !strcmp(msg_info->to, (first+i)->id))
                write((first+i)->fd, msg_info->msg, msg_info->len);
    }
}

/* ════════════════════════════════════
 *  main
 * ════════════════════════════════════ */
int main(int argc, char *argv[])
{
    int  serv_sock, clnt_sock;
    struct sockaddr_in serv_adr, clnt_adr;
    int  clnt_adr_sz;
    int  sock_option = 1;
    pthread_t t_id[MAX_CLNT] = {0};
    int  str_len = 0, i = 0;
    char idpasswd[(ID_SIZE*2)+3];
    char *pToken, *pArray[ARR_CNT] = {0};
    char msg[BUF_SIZE];

    if (argc != 2) { printf("Usage: %s <port>\n", argv[0]); exit(1); }

    if (db_init() < 0) exit(1);

    FILE *idFd = fopen("idpasswd.txt", "r");
    if (!idFd) { perror("fopen(idpasswd.txt)"); exit(1); }

    char id[ID_SIZE], pw[ID_SIZE];
    CLIENT_INFO *client_info =
        (CLIENT_INFO *)calloc(sizeof(CLIENT_INFO), MAX_CLNT);
    if (!client_info) { perror("calloc"); exit(1); }

    do {
        str_len = fscanf(idFd, "%s %s", id, pw);
        if (str_len <= 0) break;
        client_info[i].fd = -1;
        strcpy(client_info[i].id, id);
        strcpy(client_info[i].pw, pw);
        if (++i >= MAX_CLNT) break;
    } while (1);
    fclose(idFd);

    pthread_mutex_init(&mutx,    NULL);
    pthread_mutex_init(&bt_mutx, NULL);
    pthread_mutex_init(&db_mutx, NULL);

    if (bt_connect_stm32() == 0) {
        pthread_t bt_tid;
        pthread_create(&bt_tid, NULL, bt_recv_thread, NULL);
        pthread_detach(bt_tid);
    } else {
        fprintf(stderr, "[경고] STM32 BT 연결 실패\n");
    }

    pthread_t http_tid;
    pthread_create(&http_tid, NULL, http_server_thread, NULL);
    pthread_detach(http_tid);

    serv_sock = socket(PF_INET, SOCK_STREAM, 0);
    memset(&serv_adr, 0, sizeof(serv_adr));
    serv_adr.sin_family      = AF_INET;
    serv_adr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_adr.sin_port        = htons(atoi(argv[1]));
    setsockopt(serv_sock, SOL_SOCKET, SO_REUSEADDR,
               &sock_option, sizeof(sock_option));
    if (bind(serv_sock, (struct sockaddr *)&serv_adr, sizeof(serv_adr)) == -1)
        error_handling("bind() error");
    if (listen(serv_sock, 5) == -1)
        error_handling("listen() error");

    printf("=== SmartFarm IoT Server ===\n");
    printf("TCP  포트: %s\n", argv[1]);
    printf("HTTP 포트: %d\n", HTTP_PORT);
    printf("계절 기준: 겨울<%.0f°C / 여름>%.0f°C\n", TEMP_WINTER, TEMP_SUMMER);
    fputs("IoT Server Start!!\n", stdout);

    while (1) {
        clnt_adr_sz = sizeof(clnt_adr);
        clnt_sock   = accept(serv_sock,
                             (struct sockaddr *)&clnt_adr, &clnt_adr_sz);
        if (clnt_cnt >= MAX_CLNT) {
            printf("socket full\n");
            shutdown(clnt_sock, SHUT_WR); continue;
        } else if (clnt_sock < 0) {
            perror("accept()"); continue;
        }

        str_len = read(clnt_sock, idpasswd, sizeof(idpasswd));
        idpasswd[str_len] = '\0';

        if (str_len > 0) {
            i = 0;
            pToken = strtok(idpasswd, "[:]");
            while (pToken != NULL) {
                pArray[i] = pToken;
                if (i++ >= ARR_CNT) break;
                pToken = strtok(NULL, "[:]");
            }
            for (i = 0; i < MAX_CLNT; i++) {
                if (!strcmp(client_info[i].id, pArray[0])) {
                    if (client_info[i].fd != -1) {
                        sprintf(msg, "[%s] Already logged!\n", pArray[0]);
                        write(clnt_sock, msg, strlen(msg));
                        log_file(msg);
                        shutdown(clnt_sock, SHUT_WR);
                        client_info[i].fd = -1;
                        break;
                    }
                    if (!strcmp(client_info[i].pw, pArray[1])) {
                        strcpy(client_info[i].ip, inet_ntoa(clnt_adr.sin_addr));
                        pthread_mutex_lock(&mutx);
                        client_info[i].index = i;
                        client_info[i].fd    = clnt_sock;
                        clnt_cnt++;
                        pthread_mutex_unlock(&mutx);
                        sprintf(msg, "[%s] New connected! (ip:%s,fd:%d,cnt:%d)\n",
                            pArray[0], inet_ntoa(clnt_adr.sin_addr),
                            clnt_sock, clnt_cnt);
                        log_file(msg);
                        write(clnt_sock, msg, strlen(msg));
                        pthread_create(t_id+i, NULL, clnt_connection, client_info+i);
                        pthread_detach(t_id[i]);
                        break;
                    }
                }
            }
            if (i == MAX_CLNT) {
                sprintf(msg, "[%s] Authentication Error!\n", pArray[0]);
                write(clnt_sock, msg, strlen(msg));
                log_file(msg);
                shutdown(clnt_sock, SHUT_WR);
            }
        } else
            shutdown(clnt_sock, SHUT_WR);
    }
    return 0;
}

void error_handling(char *msg)
{
    fputs(msg, stderr); fputc('\n', stderr); exit(1);
}
void log_file(char *msgstr) { fputs(msgstr, stdout); }
void getlocaltime(char *buf)
{
    struct tm *t; time_t tt = time(NULL);
    char wday[7][4]={"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    t = localtime(&tt);
    sprintf(buf,"[GETTIME]%02d.%02d.%02d %02d:%02d:%02d %s",
            t->tm_year-100,t->tm_mon+1,t->tm_mday,
            t->tm_hour,t->tm_min,t->tm_sec,wday[t->tm_wday]);
}

