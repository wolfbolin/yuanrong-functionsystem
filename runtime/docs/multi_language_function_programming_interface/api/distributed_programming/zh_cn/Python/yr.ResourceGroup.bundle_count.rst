.. _bundle_count:

yr.ResourceGroup.bundle_count
-------------------------------------

.. py:attribute:: ResourceGroup.bundle_count: int
   :type: int

   返回当前资源组中的 bundle 数量。

   返回：
       当前资源组下的所有 bundle。
       数据类型为 List[Dict]。

   样例:
       >>> rg = yr.create_resource_group([{"NPU":1},{"CPU":2000,"Memory":2000}], "rgname")
       >>> bundles = rg.bundle_specs


