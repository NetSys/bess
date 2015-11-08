class Port(object):
    def __init__(self, driver = 'PMD', name = None, arg = None, **kwargs):
        self.name = '<uninitialized>'
        self.driver = driver

        ret = self.softnic.create_port(driver, name, 
                self.choose_arg(arg, kwargs))

        self.name = ret['name']
        #print 'Port %s created' % self

    def __str__(self):
        return '%s::%s' % (self.name, self.driver)

    def get_port_stats(self):
        return self.softnic.get_port_stats(self.name)
