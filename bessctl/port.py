class Port(object):

    def __init__(self, **kwargs):
        self.name = '<uninitialized>'
        self.driver = self.__class__.__name__

        assert self.driver != 'Port', "Do not instantiate 'Port' directly"

        name = kwargs.pop('name', None)

        ret = self.bess.create_port(self.driver, name,
                                    self.choose_arg(None, kwargs))

        self.name = ret.name
        self.mac_addr = ret.mac_addr

    def __str__(self):
        return '%s/%s' % (self.name, self.driver)

    def get_port_stats(self):
        return self.bess.get_port_stats(self.name)

    def get_link_status(self):
        return self.bess.get_link_status(self.name)
