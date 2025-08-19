
//建一个物理页面引用的数组且配置各种转换函数
#define PG2REFIDX(pa) ((((uint64)pa) - KERNBASE) / PGSIZE)  //将实际地址转换成数组索引
#define MXIDX PG2REFIDX(PHYSTOP)                           //最大索引id
#define PG2CNT(pa) pg_refcnt[PG2REFIDX((pa))]                   //宏定义调用接口
extern int pg_refcnt[]; //定义每个页引用的函数