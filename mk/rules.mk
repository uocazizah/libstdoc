# yupiter - compilation rules
#
# Every object carries a dependency file produced by "-MMD -MP", so a
# header change rebuilds exactly the translation units that include it.

$(OBJDIR)/%.o: %.cxx
	@mkdir -p $(dir $@)
	$(CXX) $(LIB_CXXFLAGS) $(LIB_CPPFLAGS) -MMD -MP -c $< -o $@

$(OBJDIR)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(LIB_ASFLAGS) -MMD -MP -c $< -o $@

-include $(ARCHIVE_OBJECTS:.o=.d) $(SHARED_OBJECTS:.o=.d)
