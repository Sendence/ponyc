# Creating a Release

## Without Merging Upstream
  - make sure you have pulled the latest codebase
  - test that everything builds correctly with your updates
  - run `git tag --list` to see the latest tags
  - run `git tag sendence-x.y.z` making sure to increment the latest tag according to SemVer based on your updates
  - run `git push origin master` to push up your code if not done so already
  - run `git push origin 'your-tag'` to push your tag

## With Merging Upstream
  - run `git remote show` to see if you may already have `upstream` present
    - if not run `git remote add upstream https://github.com/ponylang/ponyc.git`
    - re-run `git remote show` to verify `upstream` is present
  - run `git fetch upstream`
  - run `git merge upstream master`
  - test that everything builds correctly with the merge
  - run `git tag --list` to see the latest tags
  - run `git tag sendence-x.y.z` making sure to increment the latest tag according to SemVer based on your updates
  - run `git push origin master` to push up your code if not done so already
  - run `git push origin 'your-tag'` to push your tag

Note: Once a tag is pushed, Drone will run a build for that tag and it will be available via docker after the build passes
