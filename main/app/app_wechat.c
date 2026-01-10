#include "app_wechat.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "app_wechat";

// 全局配置
static char *g_server_url = NULL;
static float g_price_per_kg = 10.0f;  // 默认每公斤10元
static bool g_initialized = false;

// 内部函数声明
static esp_err_t send_http_request(const char *url, const char *json_data, char **response);
static char* create_payment_request_json(float weight);
static esp_err_t parse_payment_response(const char *response, wechat_server_response_t *server_response);
static void cleanup_server_response(wechat_server_response_t *response);

esp_err_t app_wechat_init(void)
{
    ESP_LOGI(TAG, "初始化微信支付模块");
    
    if (g_initialized) {
        ESP_LOGW(TAG, "微信支付模块已初始化");
        return ESP_OK;
    }
    
    // 设置默认服务器URL
    if (!g_server_url) {
        g_server_url = strdup("https://your-server.com/api/payment");
        if (!g_server_url) {
            ESP_LOGE(TAG, "分配服务器URL内存失败");
            return ESP_ERR_NO_MEM;
        }
    }
    
    g_initialized = true;
    ESP_LOGI(TAG, "微信支付模块初始化完成");
    return ESP_OK;
}

esp_err_t app_wechat_request_payment_url(float weight, char **payment_url, char **order_id)
{
    if (!g_initialized) {
        ESP_LOGE(TAG, "模块未初始化");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!payment_url || !order_id) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (weight <= 0) {
        ESP_LOGE(TAG, "无效的重量值: %.2f", weight);
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "请求支付URL，重量: %.2f kg", weight);
    
    // 创建支付请求JSON
    char *json_data = create_payment_request_json(weight);
    if (!json_data) {
        ESP_LOGE(TAG, "创建支付请求JSON失败");
        return ESP_ERR_NO_MEM;
    }
    
    // 发送HTTP请求
    char *response = NULL;
    esp_err_t ret = send_http_request(g_server_url, json_data, &response);
    
    free(json_data);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "发送HTTP请求失败: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 解析响应
    wechat_server_response_t server_response;
    ret = parse_payment_response(response, &server_response);
    
    free(response);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "解析支付响应失败");
        return ret;
    }
    
    if (!server_response.success) {
        ESP_LOGE(TAG, "服务器返回错误: %s", server_response.error_message);
        cleanup_server_response(&server_response);
        return ESP_FAIL;
    }
    
    // 返回结果
    *payment_url = server_response.payment_url ? strdup(server_response.payment_url) : NULL;
    *order_id = server_response.order_id ? strdup(server_response.order_id) : NULL;
    
    cleanup_server_response(&server_response);
    
    if (!*payment_url || !*order_id) {
        ESP_LOGE(TAG, "复制支付URL或订单ID失败");
        return ESP_ERR_NO_MEM;
    }
    
    ESP_LOGI(TAG, "支付URL请求成功，订单ID: %s", *order_id);
    return ESP_OK;
}

esp_err_t app_wechat_poll_payment_status(const char *order_id, wechat_payment_status_t *status)
{
    if (!g_initialized) {
        ESP_LOGE(TAG, "模块未初始化");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!order_id || !status) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGD(TAG, "轮询支付状态，订单ID: %s", order_id);
    
    // 构建状态查询URL
    char *status_url = NULL;
    asprintf(&status_url, "%s/status/%s", g_server_url, order_id);
    
    if (!status_url) {
        ESP_LOGE(TAG, "构建状态查询URL失败");
        return ESP_ERR_NO_MEM;
    }
    
    // 发送HTTP GET请求
    char *response = NULL;
    esp_err_t ret = send_http_request(status_url, NULL, &response);
    
    free(status_url);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "查询支付状态失败");
        return ret;
    }
    
    // 解析状态响应
    cJSON *json = cJSON_Parse(response);
    free(response);
    
    if (!json) {
        ESP_LOGE(TAG, "解析状态JSON失败");
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    cJSON *status_json = cJSON_GetObjectItem(json, "status");
    if (!status_json || !cJSON_IsString(status_json)) {
        ESP_LOGE(TAG, "状态字段不存在或格式错误");
        cJSON_Delete(json);
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    const char *status_str = status_json->valuestring;
    
    // 映射状态
    if (strcmp(status_str, "pending") == 0) {
        *status = WECHAT_STATUS_PENDING;
    } else if (strcmp(status_str, "paid") == 0) {
        *status = WECHAT_STATUS_PAID;
    } else if (strcmp(status_str, "failed") == 0) {
        *status = WECHAT_STATUS_FAILED;
    } else if (strcmp(status_str, "timeout") == 0) {
        *status = WECHAT_STATUS_TIMEOUT;
    } else if (strcmp(status_str, "cancelled") == 0) {
        *status = WECHAT_STATUS_CANCELLED;
    } else {
        *status = WECHAT_STATUS_ERROR;
    }
    
    cJSON_Delete(json);
    
    ESP_LOGD(TAG, "支付状态: %d", *status);
    return ESP_OK;
}

esp_err_t app_wechat_generate_qrcode(const char *url, char **qr_code_data)
{
    if (!url || !qr_code_data) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "生成二维码，URL: %s", url);
    
    // 这里应该使用qrencode库生成二维码
    // 为了简化，我们直接返回URL作为占位符
    *qr_code_data = strdup(url);
    
    if (!*qr_code_data) {
        ESP_LOGE(TAG, "生成二维码数据失败");
        return ESP_ERR_NO_MEM;
    }
    
    return ESP_OK;
}

void app_wechat_cleanup(void)
{
    if (g_server_url) {
        free(g_server_url);
        g_server_url = NULL;
    }
    
    g_initialized = false;
    ESP_LOGI(TAG, "微信支付模块清理完成");
}

esp_err_t app_wechat_set_server_url(const char *url)
{
    if (!url) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (g_server_url) {
        free(g_server_url);
    }
    
    g_server_url = strdup(url);
    if (!g_server_url) {
        ESP_LOGE(TAG, "设置服务器URL失败");
        return ESP_ERR_NO_MEM;
    }
    
    ESP_LOGI(TAG, "服务器URL更新: %s", g_server_url);
    return ESP_OK;
}

esp_err_t app_wechat_set_price_per_kg(float price)
{
    if (price <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    g_price_per_kg = price;
    ESP_LOGI(TAG, "每公斤价格更新: %.2f 元", g_price_per_kg);
    return ESP_OK;
}

static esp_err_t send_http_request(const char *url, const char *json_data, char **response)
{
    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 10000,
        .buffer_size = 4096,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "初始化HTTP客户端失败");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (json_data) {
        esp_http_client_set_method(client, HTTP_METHOD_POST);
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, json_data, strlen(json_data));
    } else {
        esp_http_client_set_method(client, HTTP_METHOD_GET);
    }
    
    esp_err_t err = esp_http_client_perform(client);
    
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        int content_length = esp_http_client_get_content_length(client);
        
        ESP_LOGD(TAG, "HTTP响应状态: %d, 内容长度: %d", status_code, content_length);
        
        if (status_code == 200 && content_length > 0) {
            *response = malloc(content_length + 1);
            if (*response) {
                int read_len = esp_http_client_read(client, *response, content_length);
                (*response)[read_len] = '\0';
                ESP_LOGD(TAG, "响应内容: %s", *response);
            }
        }
    }
    
    esp_http_client_cleanup(client);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP请求失败: %s", esp_err_to_name(err));
        return err;
    }
    
    if (!*response) {
        ESP_LOGE(TAG, "获取响应内容失败");
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    return ESP_OK;
}

static char* create_payment_request_json(float weight)
{
    cJSON *json = cJSON_CreateObject();
    if (!json) {
        return NULL;
    }
    
    float amount = weight * g_price_per_kg;
    
    cJSON_AddNumberToObject(json, "weight", weight);
    cJSON_AddNumberToObject(json, "amount", amount);
    cJSON_AddStringToObject(json, "description", "智能体重秤测量费用");
    cJSON_AddNumberToObject(json, "timeout_seconds", 300);
    
    char *json_string = cJSON_Print(json);
    cJSON_Delete(json);
    
    return json_string;
}

static esp_err_t parse_payment_response(const char *response, wechat_server_response_t *server_response)
{
    if (!response || !server_response) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(server_response, 0, sizeof(wechat_server_response_t));
    
    cJSON *json = cJSON_Parse(response);
    if (!json) {
        ESP_LOGE(TAG, "解析JSON失败");
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    cJSON *success = cJSON_GetObjectItem(json, "success");
    if (success && cJSON_IsBool(success)) {
        server_response->success = cJSON_IsTrue(success);
    }
    
    if (server_response->success) {
        cJSON *order_id = cJSON_GetObjectItem(json, "order_id");
        if (order_id && cJSON_IsString(order_id)) {
            server_response->order_id = strdup(order_id->valuestring);
        }
        
        cJSON *payment_url = cJSON_GetObjectItem(json, "payment_url");
        if (payment_url && cJSON_IsString(payment_url)) {
            server_response->payment_url = strdup(payment_url->valuestring);
        }
        
        cJSON *qr_code = cJSON_GetObjectItem(json, "qr_code");
        if (qr_code && cJSON_IsString(qr_code)) {
            server_response->qr_code_data = strdup(qr_code->valuestring);
        }
        
        cJSON *timeout = cJSON_GetObjectItem(json, "timeout_seconds");
        if (timeout && cJSON_IsNumber(timeout)) {
            server_response->timeout_seconds = timeout->valueint;
        }
    } else {
        cJSON *error_msg = cJSON_GetObjectItem(json, "error_message");
        if (error_msg && cJSON_IsString(error_msg)) {
            server_response->error_message = strdup(error_msg->valuestring);
        }
    }
    
    cJSON_Delete(json);
    return ESP_OK;
}

static void cleanup_server_response(wechat_server_response_t *response)
{
    if (!response) {
        return;
    }
    
    if (response->error_message) {
        free(response->error_message);
        response->error_message = NULL;
    }
    
    if (response->order_id) {
        free(response->order_id);
        response->order_id = NULL;
    }
    
    if (response->payment_url) {
        free(response->payment_url);
        response->payment_url = NULL;
    }
    
    if (response->qr_code_data) {
        free(response->qr_code_data);
        response->qr_code_data = NULL;
    }
}