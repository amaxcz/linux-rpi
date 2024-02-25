#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sysfs.h>
#include <linux/pwm.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/mutex.h>

#define MY_DEBUG 0

#define DEVICE_NAME "my_driver"
#define MAX_HZ 2000000
#define MAX_DUTY 255

static DEFINE_MUTEX(my_mutex);

static struct pwm_device *pwm, *pwm0, *pwm1;

/* ---------------------------- sysfs attributes ---------------------------- */

static int pwm_channel, hz, duty_cycle, enable;


/* -------------------------- print_pwm_device_info ------------------------- */

void print_pwm_device_info(struct pwm_device *pwm) {
    pr_info("print_pwm_device_info (pwm-%d):\n", pwm->pwm);
    pr_info("  label: %s\n", pwm->label);
    pr_info("  flags: %lu\n", pwm->flags);
    pr_info("  hwpwm: %u\n", pwm->hwpwm);
    pr_info("  pwm: %u\n", pwm->pwm);
    pr_info("  chip: %pK\n", pwm->chip);
}

/* -------------------------- print_pwm_state_info -------------------------- */

void print_pwm_state_info(struct pwm_device *pwm) {
    struct pwm_state state;
    pwm_get_state(pwm, &state);
    pr_info("print_pwm_state_info (pwm-%d):\n", pwm->pwm);
    pr_info("  enabled: %d\n", state.enabled);
    pr_info("  period: %llu us\n", state.period);
    pr_info("  duty_cycle: %llu us\n", state.duty_cycle);
    pr_info("  polarity: %d\n", state.polarity);
    pr_info("  usage_power: %d\n", state.usage_power);
}

/* ----------------------------- set_pwm_params ----------------------------- */

void set_pwm_params(struct pwm_device *pwm, unsigned int duty_cycle, int hz) {
    int ret;
    struct pwm_state state;
    uint64_t period_ns = NSEC_PER_SEC / hz;
    uint64_t duty_cycle_ns = period_ns * duty_cycle / (MAX_DUTY + 1);;

    if (duty_cycle > MAX_DUTY && duty_cycle < 0) {
        pr_err("Duty cycle must be between 0 and 255 (inclusive).\n");
        return;
    }

    if (hz <= 0 || hz >= MAX_HZ) {
        pr_err("Frequency must be a positive number.\n");
        return;
    }

    pwm_init_state(pwm, &state);
    state.duty_cycle = duty_cycle_ns;
    state.period = period_ns;
    state.polarity = PWM_POLARITY_NORMAL;
    ret = pwm_config(pwm, duty_cycle_ns, period_ns);
    if (ret < 0) {
        pr_err("pwm_config failed: %d\n", ret);
        return;
    }
    ret = pwm_apply_state(pwm, &state);
    if (ret < 0) {
        pr_err("pwm_apply_state failed: %d\n", ret);
        return;
    }
    pr_info("set_pwm_params (pwm-%d):\n", pwm->pwm);
    pr_info("  PWM duty cycle: %u\n", duty_cycle);
    pr_info("  Actual duty cycle: %llu us\n", state.duty_cycle);
    pr_info("  PWM frequency: %d Hz\n", hz);
    pr_info("  Actual period: %llu us\n", state.period); 
}

void update_pwm_channel(int ch) {
    if (ch == 0 &&!IS_ERR(pwm0)) {
        pwm = pwm0;
    } else if (ch == 1 && !IS_ERR(pwm1)) {
        pwm = pwm1;
    } else {
        pwm = NULL;
    }
}

/* ------------------------------------ - ----------------------------------- */
void validate_pwm_channel(void) {
    if (pwm_channel < 0) {
        pwm_channel = 0;
    } else if (pwm_channel > 1) {
        pwm_channel = 1;
    }
}

void validate_duty_cycle(void) {
    if (duty_cycle <= 0) {
        duty_cycle = 1;
    } else if (duty_cycle > MAX_DUTY) {
        duty_cycle = MAX_DUTY;
    }
}

void validate_hz(void){
    if (hz <= 0) {
        hz = 1;
    } else if (hz >= MAX_HZ) {
        hz = MAX_HZ;
    }
}

void validate_enable(void) {
    if (enable < 0) {
        enable = 0;
    } else if (enable > 1) {
        enable = 1;
    }
}
/* ----------------------------- get_pwm_device ----------------------------- */

int get_pwm_device(struct pwm_device **pwm_device_ptr, int pwm_number) {
    int ret; 
    *pwm_device_ptr = pwm_request(pwm_number, DEVICE_NAME);
    if (IS_ERR(*pwm_device_ptr)) {
        ret = PTR_ERR(*pwm_device_ptr);
        if (ret != -EPROBE_DEFER) {
            pr_err("pwm_request failed for pwm-%d: %d\n", pwm_number, ret);
        }
        return ret; 
    }
    print_pwm_device_info(*pwm_device_ptr);
    return 0;
}

/* ------------------------------- pwm_channel ------------------------------ */

static ssize_t pwm_channel_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    pr_debug("pwm_channel_show\n");
    return sprintf(buf, "%d\n", pwm_channel);
}

static ssize_t pwm_channel_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
    mutex_lock(&my_mutex);
    pr_debug("pwm_channel_store\n");
    sscanf(buf, "%d", &pwm_channel);
    validate_pwm_channel();
    pwm_disable(pwm);
    enable = 0;
    update_pwm_channel(pwm_channel);
    set_pwm_params(pwm, duty_cycle, hz);
    mutex_unlock(&my_mutex);
    return count;
}

static struct kobj_attribute pwm_channel_attribute = __ATTR(pwm_channel, 0660, pwm_channel_show, pwm_channel_store);

/* ------------------------------- duty_cycle ------------------------------ */

static ssize_t duty_cycle_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    pr_debug("duty_cycle_show\n");
    return sprintf(buf, "%d\n", duty_cycle);
}

static ssize_t duty_cycle_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
    mutex_lock(&my_mutex);
    pr_debug("duty_cycle_store\n");
    sscanf(buf, "%d", &duty_cycle);
    validate_duty_cycle();
    set_pwm_params(pwm, duty_cycle, hz);
    mutex_unlock(&my_mutex);
    return count;
}

static struct kobj_attribute duty_cycle_attribute = __ATTR(duty_cycle, 0660, duty_cycle_show, duty_cycle_store);

/* ----------------------------------- hz ----------------------------------- */

static ssize_t hz_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    pr_debug("hz_show\n");
    return sprintf(buf, "%d\n", hz);
}

static ssize_t hz_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
    mutex_lock(&my_mutex);
    pr_debug("hz_store\n");
    sscanf(buf, "%d", &hz);
    validate_hz();
    set_pwm_params(pwm, duty_cycle, hz);
    mutex_unlock(&my_mutex);
    return count;
}

static struct kobj_attribute hz_attribute = __ATTR(hz, 0660, hz_show, hz_store);

/* --------------------------------- enable --------------------------------- */

static ssize_t enable_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    pr_debug("enable_show\n");
    return sprintf(buf, "%d\n", enable);
}

static ssize_t enable_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
    int ret;
    mutex_lock(&my_mutex);
    pr_debug("enable_store\n");
    sscanf(buf, "%d", &enable);
    validate_enable();
    if (enable == 1){
        set_pwm_params(pwm, duty_cycle, hz);
        ret = pwm_enable(pwm);
            if (ret < 0){
                pr_err("pwm_enable failed: %d\n", ret);
                enable = 0;
            }
    } else {
        pwm_disable(pwm);
        enable = 0;
    }
    mutex_unlock(&my_mutex);
    return count;
}

static struct kobj_attribute enable_attribute = __ATTR(enable, 0660, enable_show, enable_store);

/* ------------------------------------ - ----------------------------------- */

static struct kobject *my_kobj;

/* ------------------------------------ - ----------------------------------- */



static int __init my_test_init(void)
{
    int ret;

    pr_debug("__init begin\n");


    // Initialize PWM devices
    get_pwm_device(&pwm0, 0);
    get_pwm_device(&pwm1, 1);



    // For debugging purposes
    if(MY_DEBUG){
        // set default PWM
        if (pwm0 && !IS_ERR(pwm0)) {
            pwm = pwm0;
        } else if (pwm1 && !IS_ERR(pwm1)) {
            pwm = pwm1;
        } else {
            pr_err("No PWM device found\n");
            return -ENODEV;
        }
        
        print_pwm_state_info(pwm);

        // set_pwm_params(pwm, 200, 1);
        set_pwm_params(pwm, 32, 2);
        ret = pwm_enable(pwm);
        if (ret < 0)
            pr_err("pwm_enable failed: %d\n", ret);
    } else {
        // Probe/dts mode

        update_pwm_channel(pwm_channel);

        if (pwm) {
            print_pwm_state_info(pwm);
            if (hz > 0 && duty_cycle > 0){
                set_pwm_params(pwm, duty_cycle, hz);
                if (enable == 1){
                    ret = pwm_enable(pwm);
                    if (ret < 0){
                        pr_err("pwm_enable failed: %d\n", ret);
                        enable = 0;
                    } else {
                        print_pwm_state_info(pwm);
                    }
                }
            }
        } else {
            enable = 0;
        }
    }
    
    pr_debug("__init end\n");

    return ret;
}

static void __exit my_test_exit(void)
{
    kobject_put(my_kobj);

    if (pwm0) {
        pwm_disable(pwm0);
        pwm_free(pwm0);
    }
    if (pwm1) {
        pwm_disable(pwm1);
        pwm_free(pwm1);
    }

    pr_debug("__exit\n");
}




static int test_probe(struct platform_device *pdev){
    struct device_node *np = pdev->dev.of_node;
    int _pwm_channel, _hz, _duty_cycle, _enable;
    int ret;

    my_kobj = kobject_create_and_add(DEVICE_NAME, kernel_kobj);
    if (!my_kobj){
        pr_err("kobject_create_and_add failed\n");
        return -ENOMEM;
    }


    ///////////////////////////////////////////////////////////////////////

    ret = of_property_read_u32(np, "pwm_channel", &_pwm_channel);
    if (ret) {
        dev_err(&pdev->dev, "Failed to read device tree: pwm_channel\n");
        _pwm_channel = 0;
    } else {
        dev_info(&pdev->dev, "pwm_channel = %d\n", _pwm_channel);
    }
    ret = sysfs_create_file(my_kobj, &pwm_channel_attribute.attr);
    if (ret)
        kobject_put(my_kobj);

    pwm_channel = _pwm_channel;
    validate_pwm_channel();

    ///////////////////////////////////////////////////////////////////////

    ret = of_property_read_u32(np, "hz", &_hz);
    if (ret) {
        dev_err(&pdev->dev, "Failed to read device tree: hz\n");
        _hz = 2;
    } else {
        dev_info(&pdev->dev, "hz = %d\n", _hz);
    }
    ret = sysfs_create_file(my_kobj, &hz_attribute.attr);
    if (ret)
        kobject_put(my_kobj);

    hz = _hz;
    validate_hz();


    ///////////////////////////////////////////////////////////////////////

    ret = of_property_read_u32(np, "duty_cycle", &_duty_cycle);
    if (ret) {
        dev_err(&pdev->dev, "Failed to read device tree: duty_cycle\n");
        _duty_cycle = 32;
    } else {
        dev_info(&pdev->dev, "duty_cycle = %d\n", _duty_cycle);
    }
    ret = sysfs_create_file(my_kobj, &duty_cycle_attribute.attr);
    if (ret)
        kobject_put(my_kobj);

    duty_cycle = _duty_cycle;
    validate_duty_cycle();

    ///////////////////////////////////////////////////////////////////////

    ret = of_property_read_u32(np, "enable", &_enable);
    if (ret) {
        dev_err(&pdev->dev, "Failed to read device tree: enable\n");
        _enable = 1;
    } else {
        dev_info(&pdev->dev, "enable = %d\n", _enable);
    }
    ret = sysfs_create_file(my_kobj, &enable_attribute.attr);
    if (ret)
        kobject_put(my_kobj);
    
    enable = _enable;
    validate_enable();

    ///////////////////////////////////////////////////////////////////////

    // static initialization
    my_test_init();

    return 0;
}

static int test_remove(struct platform_device *pdev){
    my_test_exit();
    return 0;
}

static const struct of_device_id of_test_match[] = {
        { .compatible = "my-driver", },
        {}
};
MODULE_DEVICE_TABLE(of, of_test_match);

static struct platform_driver test_driver = {
        .probe          = test_probe,
        .remove         = test_remove,
        .driver         = {
                .name   = DEVICE_NAME   ,
                .of_match_table = of_test_match,
        },
};
module_platform_driver(test_driver);

// For manual testing without .dts and probing
// module_init(my_test_init);
// module_exit(my_test_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Aleksey Maximov <amaxcz@gmail.com>");
