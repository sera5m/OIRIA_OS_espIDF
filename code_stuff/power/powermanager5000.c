#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* ---------------- Config ---------------- */
static const bool useStaticDefinesForPowermanager = true; // switch between bitmask vs dynamic

/* ---------------- Base power state ---------------- */
typedef enum {
    POWER_STAY_ON = 0,
    POWER_LIGHT_SLEEP,
    POWER_DEEP_SLEEP
} powerplan_base_state_t;

/* ---------------- Bitmask definitions ---------------- */
typedef enum {
    CONN_BLUETOOTH = 0,
    CONN_WIFI,
    CONN_EXTERN_GPIO,
    CONN_OTHER,
    CONN_COUNT
} powerplan_connectivity_static_t;

typedef uint32_t powerplan_connectivity_mask_t;
#define CONN_BIT(x) (1U << (x))

typedef enum {
    SENSOR_HR = 0,
    SENSOR_MPU,
    SENSOR_TEMP,
    SENSOR_COUNT
} powerplan_sensor_static_t;

typedef uint32_t powerplan_sensor_mask_t;
#define SENSOR_BIT(x) (1U << (x))

/* ---------------- Dynamic definitions ---------------- */
typedef struct {
    char *name;
    bool enabled;
    float refresh_rate_s;
} connectivity_item_t;

typedef struct {
    char *name;
    bool enabled;
    float refresh_rate_s;
} sensor_item_t;

/* ---------------- Composite plan ---------------- */
typedef struct {
    powerplan_base_state_t base_state;

    // either/or
    union {
        struct {
            powerplan_connectivity_mask_t connectivity_mask;
            powerplan_sensor_mask_t sensor_mask;
        } static_plan;

        struct {
            connectivity_item_t *connectivity;
            size_t connectivity_count;
            sensor_item_t *sensors;
            size_t sensor_count;
        } dynamic_plan;
    };
} composite_user_power_plan_t;

/* ---------------- Creation helpers ---------------- */
composite_user_power_plan_t* create_power_plan_dynamic(size_t n_connectivity, size_t n_sensors) {
    composite_user_power_plan_t *plan = malloc(sizeof(*plan));
    plan->base_state = POWER_STAY_ON;

    plan->dynamic_plan.connectivity = calloc(n_connectivity, sizeof(connectivity_item_t));
    plan->dynamic_plan.connectivity_count = n_connectivity;

    plan->dynamic_plan.sensors = calloc(n_sensors, sizeof(sensor_item_t));
    plan->dynamic_plan.sensor_count = n_sensors;

    return plan;
}

void free_power_plan_dynamic(composite_user_power_plan_t *plan) {
    for (size_t i = 0; i < plan->dynamic_plan.connectivity_count; i++)
        free(plan->dynamic_plan.connectivity[i].name);

    for (size_t i = 0; i < plan->dynamic_plan.sensor_count; i++)
        free(plan->dynamic_plan.sensors[i].name);

    free(plan->dynamic_plan.connectivity);
    free(plan->dynamic_plan.sensors);
    free(plan);
}

/* ---------------- Iteration helpers ---------------- */
static inline void iterate_connectivity(composite_user_power_plan_t *plan, void (*callback)(const char *name)) {
    if (useStaticDefinesForPowermanager) {
        for (int i = 0; i < CONN_COUNT; i++) {
            if (plan->static_plan.connectivity_mask & CONN_BIT(i)) {
                switch(i){
                    case CONN_BLUETOOTH: callback("bluetooth"); break;
                    case CONN_WIFI: callback("wifi"); break;
                    case CONN_EXTERN_GPIO: callback("extern_gpio"); break;
                    case CONN_OTHER: callback("other"); break;
                }
            }
        }
    } else {
        for (size_t i = 0; i < plan->dynamic_plan.connectivity_count; i++) {
            if (plan->dynamic_plan.connectivity[i].enabled) {
                callback(plan->dynamic_plan.connectivity[i].name);
            }
        }
    }
}

static inline void iterate_sensors(composite_user_power_plan_t *plan, void (*callback)(const char *name)) {
    if (useStaticDefinesForPowermanager) {
        for (int i = 0; i < SENSOR_COUNT; i++) {
            if (plan->static_plan.sensor_mask & SENSOR_BIT(i)) {
                switch(i){
                    case SENSOR_HR: callback("heartrate"); break;
                    case SENSOR_MPU: callback("mpu"); break;
                    case SENSOR_TEMP: callback("temp"); break;
                }
            }
        }
    } else {
        for (size_t i = 0; i < plan->dynamic_plan.sensor_count; i++) {
            if (plan->dynamic_plan.sensors[i].enabled) {
                callback(plan->dynamic_plan.sensors[i].name);
            }
        }
    }
}
