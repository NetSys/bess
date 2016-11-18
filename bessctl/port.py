class Port(object):
    def __init__(self, **kwargs):
        self.name = '<uninitialized>'
        self.driver = self.__class__.__name__

        assert self.driver != 'Port', \
                "do not instantiate 'Port' directly"

        if 'name' in kwargs:
            name = kwargs['name']
            del kwargs['name']
        else:
            name = None

        ret = self.bess.create_port(self.driver, name, kwargs)

        self.name = ret.name
        #print 'Port %s created' % self

    def __str__(self):
        return '%s/%s' % (self.name, self.driver)

    def get_port_stats(self):
        return self.bess.get_port_stats(self.name)
