#include <linux/sched.h>
#include <net/genetlink.h>

#include "kprobe_genetlink.h"

extern s32 user_portid;
extern int do_kprobe_register(char func_name[]);
extern int do_kprobe_unregister(char func_name[]);

static const struct nla_policy policy[HOOK_A_MAX+1] = {
	[HOOK_A_FUNC_NAME] = { .type=NLA_NUL_STRING, .len = 52, },
	[HOOK_A_LOG] = { .type=NLA_STRING, .len = LOG_SIZE, },
};

static int do_hook_register(struct sk_buff *skb, struct genl_info *info);
static int do_hook_unregister(struct sk_buff *skb, struct genl_info *info);

static const struct genl_ops ops[] = {
	{
		.flags	= 0,
		.policy = policy,
		.cmd	= HOOK_C_FUNC_REGISTER,
		.doit	= do_hook_register,
	},
	{
		.flags	= 0,
		.policy = policy,
		.cmd	= HOOK_C_FUNC_UNREGISTER,
		.doit	= do_hook_unregister,
	},
};

static struct genl_family family = {
	.name		= "kprobe_hook_func",
	.version	= 1,
	.ops		= ops,
	.n_ops		= ARRAY_SIZE(ops),
	.module		= THIS_MODULE,
	.maxattr	= HOOK_A_MAX,
	.hdrsize	= 0,
};

extern struct net init_net;


/* buff */
int send_netlink_msg(void *buf, int size, int portid, int seq, int cmd, int attr)
{
	void *msg_head;
	struct sk_buff *skb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb)
                return -ENOMEM;

	msg_head = genlmsg_put(skb, 0, seq, &family, 0, cmd);
	if (!msg_head) {
		kfree_skb(skb);
		return -ENOMEM;
	}
	nla_put(skb, attr, size, buf);
	genlmsg_end(skb, msg_head); /* 完成netlink socket数据构造 */

	return genlmsg_unicast(&init_net, skb, portid);
}

static int do_hook_register(struct sk_buff *skb, struct genl_info *info)
{
        int ret;
        struct nlattr *nla;
        char func_name[52] = {0,};

	

	if (info->attrs[HOOK_A_FUNC_NAME]) {
		nla = info->attrs[HOOK_A_FUNC_NAME];
		memcpy(func_name, nla_data(nla), nla_len(nla));
		ret = do_kprobe_register(func_name);

                if (ret) {
                        printk("Kprobe register error\n");
                }
	}

        return 0;
}

static int do_hook_unregister(struct sk_buff *skb, struct genl_info *info)
{
        struct nlattr *nla;
        char func_name[52] = {0,};

	if (info->attrs[HOOK_A_FUNC_NAME]) {
		nla = info->attrs[HOOK_A_FUNC_NAME];
		memcpy(func_name, nla_data(nla), nla_len(nla));
                do_kprobe_unregister(func_name);
	}
        
	return 0;
}

int kprobe_netlink_init(void)
{
	int rc = genl_register_family(&family);
	if (rc == 0){
		printk("kprobenetlink family register successed.\n");
	}else{
		printk("kprobenetlink family register error.\n");
	}

	return rc;
}

int kprobe_netlink_exit(void)
{
	int ret;
	ret = genl_unregister_family(&family);

	if (ret) {
		printk("unregister netlink family error\n");
	} else {
		printk("unregister netlink family success\n");
	}

	return ret;
}

